#ifdef VNXVIDEO_BUILD_STUB
// gcc -shared -o libvnxvideo.so src/vnxvideo_stub.cpp -Iinclude/vnxvideo -DVNXVIDEO_BUILD_STUB -DVNXVIDEO_EXPORTS -fPIC
#include "vnxvideo.h"

void vnxvideo_init_ffmpeg(ELogLevel level);

void vnx_atexit() {
}

int vnxvideo_init(vnxvideo_log_t log_handler, void* usrptr, ELogLevel max_level) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_manager_dshow_create(vnxvideo_manager_t* mgr) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_manager_v4l_create(vnxvideo_manager_t* mgr) {
    return vnxvideo_err_not_implemented;
}
void vnxvideo_manager_free(vnxvideo_manager_t mgr) {
}

int vnxvideo_enumerate_video_sources(vnxvideo_manager_t mgr, int details, char* json_buffer, int buffer_size) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_video_source_create(vnxvideo_manager_t mgr, const char* address, const char* mode, vnxvideo_videosource_t* src) {
    return vnxvideo_err_not_implemented;
}
void vnxvideo_video_source_free(vnxvideo_videosource_t src) {
}

int vnxvideo_video_source_subscribe(vnxvideo_videosource_t src, 
    vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
    vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_video_source_start(vnxvideo_videosource_t src) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_video_source_stop(vnxvideo_videosource_t src) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_raw_sample_dup(vnxvideo_raw_sample_t src, vnxvideo_raw_sample_t* dst) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_raw_sample_copy(vnxvideo_raw_sample_t src, vnxvideo_raw_sample_t* dst) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_raw_sample_allocate(EColorspace csp, int width, int height, vnxvideo_raw_sample_t* dst) {
    return vnxvideo_err_not_implemented;
}

void vnxvideo_raw_sample_free(vnxvideo_raw_sample_t sample) {
}
int vnxvideo_raw_sample_get_data(vnxvideo_raw_sample_t sample, int* strides, uint8_t **planes) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_raw_sample_get_format(vnxvideo_raw_sample_t sample, EColorspace *csp, int *width, int *height) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_buffer_dup(vnxvideo_buffer_t src, vnxvideo_buffer_t* dst) {
    return vnxvideo_err_not_implemented;
}
void vnxvideo_buffer_free(vnxvideo_buffer_t sample) {
}
int vnxvideo_buffer_get_data(vnxvideo_buffer_t sample, uint8_t* *data, int* size) {
    return vnxvideo_err_not_implemented;
}

void vnxvideo_rawproc_free(vnxvideo_rawproc_t proc) {
}
int vnxvideo_rawproc_set_format(vnxvideo_rawproc_t proc, EColorspace csp, int width, int height) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_rawproc_process(vnxvideo_rawproc_t proc, vnxvideo_raw_sample_t sample, uint64_t timestamp) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_rawproc_flush(vnxvideo_rawproc_t proc) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_rawproc_create_from_callbacks(
    vnxvideo_on_frame_format_t onFormat, void* usrptrOnFormat,
    vnxvideo_on_raw_sample_t onSample, void* usrptrOnSample,
    vnxvideo_rawproc_t* proc) {
    return vnxvideo_err_not_implemented;
}


int vnxvideo_h264_encoder_create(const char* json_config, vnxvideo_encoder_t* encoder) {
    return vnxvideo_err_not_implemented;
}
vnxvideo_rawproc_t vnxvideo_encoder_to_rawproc(vnxvideo_h264_encoder_t e) {
    return vnxvideo_rawproc_t{ nullptr };
}
int vnxvideo_encoder_subscribe(vnxvideo_encoder_t encoder,
    vnxvideo_on_buffer_t handle_data, void* usrptr) {
    return vnxvideo_err_not_implemented;
}


void vnxvideo_h264_source_free(vnxvideo_h264_source_t source) {
}
int vnxvideo_h264_source_start(vnxvideo_h264_source_t source) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_h264_source_stop(vnxvideo_h264_source_t source) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_media_source_subscribe(vnxvideo_media_source_t source,
    EMediaSubtype media_subtype, vnxvideo_on_buffer_t handle_data, void* usrptr) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_media_source_events_subscribe(vnxvideo_media_source_t source,
    vnxvideo_on_json_t handle_event, void* usrptr) {
    return vnxvideo_err_not_implemented;
}
void vnxvideo_media_source_free(vnxvideo_media_source_t source) {
}
int vnxvideo_media_source_start(vnxvideo_media_source_t source) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_media_source_stop(vnxvideo_media_source_t source) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_media_source_enum_mediatypes(vnxvideo_media_source_t source,
    EMediaSubtype* buffer, int buffer_size_bytes, int* count) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_media_source_get_extradata(vnxvideo_media_source_t source,
    EMediaSubtype media_subtype, vnxvideo_buffer_t* buffer) {
    return vnxvideo_err_not_implemented;
}


vnxvideo_h264_source_t vnxvideo_media_source_to_h264(vnxvideo_media_source_t src) {
    return vnxvideo_h264_source_t{nullptr};
}
vnxvideo_media_source_t vnxvideo_h264_source_to_media(vnxvideo_h264_source_t src) {
    return vnxvideo_media_source_t{nullptr};
}



int vnxvideo_composer_create(const char* json_config, vnxvideo_composer_t* composer) {
    return vnxvideo_err_not_implemented;
}
vnxvideo_rawproc_t vnxvideo_composer_to_rawproc(vnxvideo_composer_t c) {
    return vnxvideo_rawproc_t{ nullptr };
}
int vnxvideo_composer_set_overlay(vnxvideo_composer_t c, vnxvideo_raw_sample_t s) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_raw_sample_from_bmp(const uint8_t* data, int size, vnxvideo_raw_sample_t* sample) {
    return vnxvideo_err_not_implemented;
}


int vnxvideo_h264_decoder_create(vnxvideo_decoder_t* decoder) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_h264_sw_decoder_create(vnxvideo_decoder_t* decoder) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_hevc_decoder_create(vnxvideo_decoder_t* decoder) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_hevc_sw_decoder_create(vnxvideo_decoder_t* decoder) {
    return vnxvideo_err_not_implemented;
}

void vnxvideo_decoder_free(vnxvideo_decoder_t decoder) {
}
int vnxvideo_decoder_subscribe(vnxvideo_decoder_t decoder,
    vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
    vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_decoder_decode(vnxvideo_decoder_t decoder, vnxvideo_buffer_t buffer, uint64_t timestamp) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_decoder_flush(vnxvideo_decoder_t decoder) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_analytics_create(const char* json_config, vnxvideo_analytics_t* analytics) {
    return vnxvideo_err_not_implemented;
}
vnxvideo_rawproc_t vnxvideo_analytics_to_rawproc(vnxvideo_analytics_t analytics) {
    return vnxvideo_rawproc_t{nullptr};
}
int vnxvideo_analytics_subscribe(vnxvideo_analytics_t analytics,
    vnxvideo_on_json_t handle_json, void* usrptr_json, // json for rare events
    vnxvideo_on_buffer_t handle_binary, void* usrptr_binary) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_rawproc_chain_create(vnxvideo_rawproc_chain_t* chain) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC vnxvideo_rawproc_t vnxvideo_rawproc_chain_to_rawproc(vnxvideo_rawproc_chain_t chain) {
    return vnxvideo_rawproc_t{nullptr};
}
VNXVIDEO_DECLSPEC int vnxvideo_rawproc_chain_link(vnxvideo_rawproc_chain_t chain, vnxvideo_rawproc_t link) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_rawtransform_create(const char* json_config, vnxvideo_rawtransform_t* transform) {
    return vnxvideo_err_not_implemented;
}
vnxvideo_rawproc_t vnxvideo_rawtransform_to_rawproc(vnxvideo_rawtransform_t e) {
    return vnxvideo_rawproc_t{nullptr};
}
int vnxvideo_rawtransform_subscribe(vnxvideo_rawtransform_t transform,
    vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
    vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_imganalytics_create(const char* json_config, vnxvideo_imganalytics_t *ian) {
    return vnxvideo_err_not_implemented; // its all in vnxcv library. and it requires "authentication"
}
void vnxvideo_imganalytics_free(vnxvideo_imganalytics_t ian) {
}
int vnxvideo_imganalytics_set_format(vnxvideo_imganalytics_t ian, EColorspace csp, int width, int height) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_imganalytics_process(vnxvideo_imganalytics_t ian, vnxvideo_raw_sample_t sample,
    char* /*out*/json_buffer, int* /*inout*/ buffer_size) {
    return vnxvideo_err_not_implemented;
}


int vnxvideo_renderer_create(int refresh_rate, vnxvideo_renderer_t* renderer) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC vnxvideo_videosource_t vnxvideo_renderer_to_videosource(vnxvideo_renderer_t renderer) {
    return vnxvideo_videosource_t{nullptr};
}

VNXVIDEO_DECLSPEC int vnxvideo_renderer_create_input(vnxvideo_renderer_t renderer, 
                        int index, const char* transform_json, vnxvideo_rawproc_t* input) {
    return vnxvideo_err_not_implemented;
}

VNXVIDEO_DECLSPEC int vnxvideo_renderer_update_layout(vnxvideo_renderer_t renderer,
    int width, int height, uint8_t* backgroundColor, vnxvideo_raw_sample_t backgroundImage, 
    vnxvideo_raw_sample_t nosignalImage, const char* layout) {
    return vnxvideo_err_not_implemented;
}

VNXVIDEO_DECLSPEC int vnxvideo_renderer_update_audio_layout(vnxvideo_renderer_t renderer,
    int sample_rate, int channels, const char* layout) {
    return vnxvideo_err_not_implemented;
}

VNXVIDEO_DECLSPEC int vnxvideo_renderer_set_background(vnxvideo_renderer_t renderer, 
    uint8_t* backgroundColor, vnxvideo_raw_sample_t backgroundImage) 
{
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC int vnxvideo_renderer_set_nosignal(vnxvideo_renderer_t renderer,
    vnxvideo_raw_sample_t nosignalImage)
{
    return vnxvideo_err_not_implemented;
}


VNXVIDEO_DECLSPEC int vnxvideo_with_shm_allocator_str(const char* name, int maxSizeMB, vnxvideo_action_t action, void* usrptr) {
    return vnxvideo_err_not_implemented;
}

VNXVIDEO_DECLSPEC int vnxvideo_with_shm_allocator_ptr(vnxvideo_allocator_t allocator, vnxvideo_action_t action, void* usrptr) {
    return vnxvideo_err_not_implemented;
}

VNXVIDEO_DECLSPEC void vnxvideo_shm_allocator_duplicate(vnxvideo_allocator_t* out) {
}
VNXVIDEO_DECLSPEC void vnxvideo_shm_allocator_free(vnxvideo_allocator_t allocator) {
}


VNXVIDEO_DECLSPEC int vnxvideo_local_client_create(const char* name, vnxvideo_videosource_t* out) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC int vnxvideo_local_server_create(const char* name, int maxSizeMB, vnxvideo_rawproc_t* out) {
    return vnxvideo_err_not_implemented;
}

VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_free(vnxvideo_vmsplugin_t vmsplugin) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_h264_source_create_live(vnxvideo_vmsplugin_t vmsplugin,
    const char* channel_selector, vnxvideo_h264_source_t* source) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_h264_source_create_archive(vnxvideo_vmsplugin_t vmsplugin,
    const char* channel_selector, uint64_t begin, uint64_t end, double speed,
    vnxvideo_h264_source_t* source) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_get_archive_timeline(vnxvideo_vmsplugin_t vmsplugin,
    const char* channel_selector, uint64_t begin, uint64_t end,
    vnxvideo_buffer_t* intervals) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_get_snapshot(vnxvideo_vmsplugin_t vmsplugin,
    const char* channel_selector, uint64_t timestamp, vnxvideo_buffer_t* jpeg) {
    return vnxvideo_err_not_implemented;
}

VNXVIDEO_DECLSPEC int vnxvideo_audio_encoder_create(EMediaSubtype output, const char* json_config, vnxvideo_encoder_t* encoder) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC int vnxvideo_audio_decoder_create(EMediaSubtype input,
    int channels, const uint8_t *extradata, int extradata_length,
    vnxvideo_decoder_t* decoder) {
    return vnxvideo_err_not_implemented;
}


VNXVIDEO_DECLSPEC int vnxvideo_audio_transcoder_create(EMediaSubtype output,
    EMediaSubtype input, int channels, const uint8_t *extradata, int extradata_length,
    vnxvideo_transcoder_t* transcoder) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC void vnxvideo_transcoder_free(vnxvideo_transcoder_t transcoder) {
}
VNXVIDEO_DECLSPEC int vnxvideo_transcoder_subscribe(vnxvideo_transcoder_t transcoder, vnxvideo_on_buffer_t handle_data, void* usrptr) {
    return vnxvideo_err_not_implemented;
}
VNXVIDEO_DECLSPEC int vnxvideo_transcoder_process(vnxvideo_transcoder_t transcoder, vnxvideo_buffer_t buffer, uint64_t timestamp) {
    return vnxvideo_err_not_implemented;
}

VNXVIDEO_DECLSPEC bool vnxvideo_emf_is_video(ERawMediaFormat emf) {
    return emf < EMF_AUDIO;
}
VNXVIDEO_DECLSPEC bool vnxvideo_emf_is_audio(ERawMediaFormat emf) {
    return emf > EMF_AUDIO;
}

int vnxvideo_buffer_wrap(const uint8_t *data, int size, vnxvideo_buffer_t* res) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_buffer_copy_wrap(const uint8_t *data, int size, vnxvideo_buffer_t* res) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_buffer_copy(vnxvideo_buffer_t src, vnxvideo_buffer_t* dst) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_raw_sample_select_roi(vnxvideo_raw_sample_t in,
    int roi_left, int roi_top, int roi_width, int roi_height,
    vnxvideo_raw_sample_t* out)
{
    return vnxvideo_err_not_implemented;
}

int vnxvideo_raw_sample_crop_resize(vnxvideo_raw_sample_t in,
    int roi_left, int roi_top, int roi_width, int roi_height, 
    int target_width, int target_height, 
    vnxvideo_raw_sample_t* out) 
{
    return vnxvideo_err_not_implemented;
}
int vnxvideo_raw_sample_wrap(EColorspace csp, int width, int height,
    int* strides, uint8_t **planes, vnxvideo_raw_sample_t* dst) {
    return vnxvideo_err_not_implemented;
}

int vnxvideo_h264_source_subscribe(vnxvideo_h264_source_t source,
    vnxvideo_on_buffer_t handle_data, void* usrptr) {
    return vnxvideo_err_not_implemented;
}
int vnxvideo_h264_source_events_subscribe(vnxvideo_h264_source_t source,
    vnxvideo_on_json_t handle_event, void* usrptr) {
    return vnxvideo_err_not_implemented;
}

#endif
