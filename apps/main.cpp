// main.cpp
#include <gst/gst.h>
#include <glib.h>
#include <iostream>
#include "nvdsmeta.h"
#include "gstnvdsmeta.h"
#include "nvdsinfer.h"
#include "nvdsmeta_schema.h"
#include "nvdsinfer_custom_impl.h"
#include "nvds_version.h"
#include "image_to_world.hpp"

#include <toml.hpp>
#include <vector>

struct CameraConfig {
    int width = 640, height = 480;  // default init
    float pos_x = 0.f, pos_y = 0.f, pos_z = 0.f;
    float rot_x = 0.f, rot_y = 0.f, rot_z = 0.f;
    float fov_x = 0.f, fov_y = 0.f;
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

    try {
        const auto data = toml::parse_file("config.toml");

        // resolution
        auto res_node = data["resolution"];
        if (!res_node || !res_node.is_array()) {
            std::cerr << "Missing or invalid 'resolution'\n";
            return -1;
        }
        std::vector<int> resolution;
        for (auto& el : *res_node.as_array()) {
            if (auto val = el.value<int>())
                resolution.push_back(*val);
        }
        if (resolution.size() != 2) {
            std::cerr << "resolution must have exactly 2 elements\n";
            return -1;
        }
        cfg.width = resolution[0];
        cfg.height = resolution[1];

        // position
        auto pos_node = data["position"];
        if (!pos_node || !pos_node.is_array()) {
            std::cerr << "Missing or invalid 'position'\n";
            return -1;
        }
        std::vector<double> position;
        for (auto& el : *pos_node.as_array()) {
            if (auto val = el.value<double>())
                position.push_back(*val);
        }
        if (position.size() != 3) {
            std::cerr << "position must have exactly 3 elements\n";
            return -1;
        }
        cfg.pos_x = static_cast<float>(position[0]);
        cfg.pos_y = static_cast<float>(position[1]);
        cfg.pos_z = static_cast<float>(position[2]);

        // rotation
        auto rot_node = data["rotation"];
        if (!rot_node || !rot_node.is_array()) {
            std::cerr << "Missing or invalid 'rotation'\n";
            return -1;
        }
        std::vector<double> rotation;
        for (auto& el : *rot_node.as_array()) {
            if (auto val = el.value<double>())
                rotation.push_back(*val);
        }
        if (rotation.size() != 3) {
            std::cerr << "rotation must have exactly 3 elements\n";
            return -1;
        }
        cfg.rot_x = static_cast<float>(rotation[0]);
        cfg.rot_y = static_cast<float>(rotation[1]);
        cfg.rot_z = static_cast<float>(rotation[2]);

        // fov
        auto fov_node = data["fov"];
        if (!fov_node || !fov_node.is_array()) {
            std::cerr << "Missing or invalid 'fov'\n";
            return -1;
        }
        std::vector<double> fov;
        for (auto& el : *fov_node.as_array()) {
            if (auto val = el.value<double>())
                fov.push_back(*val);
        }
        if (fov.size() != 2) {
            std::cerr << "fov must have exactly 2 elements\n";
            return -1;
        }
        cfg.fov_x = static_cast<float>(fov[0]);
        cfg.fov_y = static_cast<float>(fov[1]);

        // Print loaded config to verify
        std::cout << "Loaded config:\n";
        std::cout << " Resolution: " << cfg.width << "x" << cfg.height << "\n";
        std::cout << " Position: " << cfg.pos_x << ", " << cfg.pos_y << ", " << cfg.pos_z << "\n";
        std::cout << " Rotation: " << cfg.rot_x << ", " << cfg.rot_y << ", " << cfg.rot_z << "\n";
        std::cout << " FOV: " << cfg.fov_x << ", " << cfg.fov_y << "\n";
    }
    catch (const toml::parse_error& err) {
        std::cerr << "Parsing failed: " << err.what() << "\n";
        return -1;
    }

    // Build pipeline with resolution from config
    gchar pipeline_desc[1024];
    snprintf(pipeline_desc, sizeof(pipeline_desc),
        "nvv4l2camerasrc device=/dev/video1 ! "
        "video/x-raw(memory:NVMM), format=UYVY, width=%d, height=%d ! "
        "nvvidconv ! video/x-raw(memory:NVMM), format=I420 ! "
        "nvvidconv ! video/x-raw(memory:NVMM), format=NV12 ! "
        "nvstreammux name=mux batch-size=1 width=%d height=%d ! "
        "nvinfer config-file-path=config_infer_primary_yoloV10.txt ! "
        "nvdsosd name=osd ! "
        "nvvidconv ! nvv4l2h264enc ! rtph264pay mtu=60000 ! "
        "udpsink clients=100.72.147.81:5000 sync=false",
        cfg.width, cfg.height, cfg.width, cfg.height
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
