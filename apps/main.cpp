// main.cpp
#include <gst/gst.h>
#include <glib.h>
#include <iostream>
#include <string>
#include "nvdsmeta.h"
#include "gstnvdsmeta.h"
#include "nvdsinfer.h"
#include "nvdsmeta_schema.h"
#include "nvdsinfer_custom_impl.h"
#include "nvds_version.h"
#include "image_to_world.hpp"

#include <toml++/toml.h>

struct CameraConfig {
    std::string device;
    int width, height;
    float pos_x, pos_y, pos_z;
    float rot_x, rot_y, rot_z;
    float fov_x, fov_y;
};

static GstPadProbeReturn osd_sink_pad_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    CameraConfig *cfg = static_cast<CameraConfig *>(user_data);

    GstBuffer *buf = (GstBuffer *)info->data;
    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta) {
        return GST_PAD_PROBE_OK;
    }

    for (NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame != nullptr; l_frame = l_frame->next) {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *)(l_frame->data);

        for (NvDsMetaList *l_obj = frame_meta->obj_meta_list; l_obj != nullptr; l_obj = l_obj->next) {
            NvDsObjectMeta *obj_meta = (NvDsObjectMeta *)(l_obj->data);

            float cx = obj_meta->rect_params.left + obj_meta->rect_params.width / 2.0f;
            float cy = obj_meta->rect_params.top + obj_meta->rect_params.height / 2.0f;

            auto [wx, wy] = imageToWorld(cx, cy, cfg->width, cfg->height, cfg->fov_x, cfg->fov_y, cfg->pos_x, cfg->pos_y);

            char *label = (char *)g_malloc0(64);
            snprintf(label, 64, "X:%.2f Y:%.2f", wx, wy);
            obj_meta->text_params.display_text = label;
        }
    }

    return GST_PAD_PROBE_OK;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    CameraConfig cfg;

    // Set default values (optional)
    cfg.device = "/dev/video0";
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.pos_x = cfg.pos_y = cfg.pos_z = 0.0f;
    cfg.rot_x = cfg.rot_y = cfg.rot_z = 0.0f;
    cfg.fov_x = cfg.fov_y = 1.0f;

    // Load config.toml
    try {
        auto data = toml::parse_file("config.toml");

        if (auto device_node = data["device"].as_string())
            cfg.device = device_node->get();

        if (auto resolution_node = data["resolution"].as_array()) {
            if (resolution_node->size() == 2) {
                cfg.width = static_cast<int>(resolution_node->at(0).value_or(1920));
                cfg.height = static_cast<int>(resolution_node->at(1).value_or(1080));
            } else {
                std::cerr << "Invalid resolution size in config.toml\n";
                return -1;
            }
        } else {
            std::cerr << "Missing or invalid 'resolution' in config.toml\n";
            return -1;
        }

        if (auto position_node = data["position"].as_array()) {
            if (position_node->size() == 3) {
                cfg.pos_x = static_cast<float>(position_node->at(0).value_or(0.0));
                cfg.pos_y = static_cast<float>(position_node->at(1).value_or(0.0));
                cfg.pos_z = static_cast<float>(position_node->at(2).value_or(0.0));
            } else {
                std::cerr << "Invalid position size in config.toml\n";
                return -1;
            }
        } else {
            std::cerr << "Missing or invalid 'position' in config.toml\n";
            return -1;
        }

        if (auto rotation_node = data["rotation"].as_array()) {
            if (rotation_node->size() == 3) {
                cfg.rot_x = static_cast<float>(rotation_node->at(0).value_or(0.0));
                cfg.rot_y = static_cast<float>(rotation_node->at(1).value_or(0.0));
                cfg.rot_z = static_cast<float>(rotation_node->at(2).value_or(0.0));
            } else {
                std::cerr << "Invalid rotation size in config.toml\n";
                return -1;
            }
        } else {
            std::cerr << "Missing or invalid 'rotation' in config.toml\n";
            return -1;
        }

        if (auto fov_node = data["fov"].as_array()) {
            if (fov_node->size() == 2) {
                cfg.fov_x = static_cast<float>(fov_node->at(0).value_or(1.0));
                cfg.fov_y = static_cast<float>(fov_node->at(1).value_or(1.0));
            } else {
                std::cerr << "Invalid fov size in config.toml\n";
                return -1;
            }
        } else {
            std::cerr << "Missing or invalid 'fov' in config.toml\n";
            return -1;
        }
    }
    catch (const toml::parse_error& err) {
        std::cerr << "Parsing failed: " << err.what() << std::endl;
        return -1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error reading config.toml: " << e.what() << std::endl;
        return -1;
    }

    // Print loaded config to verify
    std::cout << "Device: " << cfg.device << std::endl;
    std::cout << "Resolution: " << cfg.width << " x " << cfg.height << std::endl;
    std::cout << "Position: (" << cfg.pos_x << ", " << cfg.pos_y << ", " << cfg.pos_z << ")\n";
    std::cout << "Rotation: (" << cfg.rot_x << ", " << cfg.rot_y << ", " << cfg.rot_z << ")\n";
    std::cout << "FOV: (" << cfg.fov_x << ", " << cfg.fov_y << ")\n";

    // Construct GStreamer pipeline description dynamically using device and resolution
    gchar pipeline_desc[2048];
    snprintf(pipeline_desc, sizeof(pipeline_desc),
        "v4l2src device=/dev/%s ! "
        "video/x-raw, width=%d, height=%d ! "
        "nvvidconv ! video/x-raw(memory:NVMM), format=I420 ! "
        "nvvidconv ! video/x-raw(memory:NVMM), format=NV12 ! "
        "nvstreammux name=mux batch-size=1 width=%d height=%d ! "
        "nvinfer config-file-path=config_infer_primary_yoloV10.txt ! "
        "nvdsosd name=osd ! "
        "nvvidconv ! nvv4l2h264enc ! rtph264pay mtu=60000 ! "
        "udpsink clients=100.72.147.81:5000 sync=false",
        cfg.device.c_str(), cfg.width, cfg.height, cfg.width, cfg.height
    );

    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipeline_desc, &error);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << error->message << std::endl;
        g_error_free(error);
        return -1;
    }

    GstElement *osd = gst_bin_get_by_name(GST_BIN(pipeline), "osd");
    GstPad *osd_sink_pad = gst_element_get_static_pad(osd, "sink");

    gst_pad_add_probe(osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER, osd_sink_pad_buffer_probe, &cfg, NULL);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return 0;
}
