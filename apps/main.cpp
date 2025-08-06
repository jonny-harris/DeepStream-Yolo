// apps/real_world_overlay/main.cpp
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

// Config struct holding camera parameters
struct CameraConfig {
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

    // Load your config here (hardcoded for now)
    CameraConfig cfg;
    cfg.width = 3848;
    cfg.height = 2168;
    cfg.pos_x = 0.750f;
    cfg.pos_y = 0.0f;
    cfg.pos_z = 0.0f;
    cfg.rot_x = 0.0f;
    cfg.rot_y = 180.0f;
    cfg.rot_z = -90.0f;
    cfg.fov_x = 0.99788205886f;
    cfg.fov_y = 0.65877955059f;

    GError *error = nullptr;
    gchar *pipeline_desc = g_strdup(
        "nvv4l2camerasrc device=/dev/video1 ! "
        "video/x-raw(memory:NVMM), format=UYVY, width=1920, height=1080 ! "
        "nvvidconv ! video/x-raw(memory:NVMM), format=I420 ! "
        "nvvidconv ! video/x-raw(memory:NVMM), format=NV12 ! "
        "nvstreammux name=mux batch-size=1 width=1920 height=1080 ! "
        "nvinfer config-file-path=config_infer_primary_yoloV10.txt ! "
        "nvdsosd name=osd ! "
        "nvvidconv ! nvv4l2h264enc ! rtph264pay mtu=60000 ! "
        "udpsink clients=100.72.147.81:5000 sync=false"
    );

    GstElement *pipeline = gst_parse_launch(pipeline_desc, &error);
    if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << error->message << std::endl;
        g_error_free(error);
        return -1;
    }

    GstElement *osd = gst_bin_get_by_name(GST_BIN(pipeline), "osd");
    GstPad *osd_sink_pad = gst_element_get_static_pad(osd, "sink");

    // Pass pointer to config struct as user_data
    gst_pad_add_probe(osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER, osd_sink_pad_buffer_probe, &cfg, NULL);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return 0;
}
