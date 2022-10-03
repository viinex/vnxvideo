#pragma once

#include <stdint.h>

#ifdef _MSC_VER
#ifdef VNXVIDEO_EXPORTS
#define VNXVIDEO_DECLSPEC __declspec(dllexport)
#else
#define VNXVIDEO_DECLSPEC __declspec(dllimport)
#endif
#else // not MSC_VER
#ifdef VNXVIDEO_EXPORTS
#define VNXVIDEO_DECLSPEC __attribute__ ((visibility ("default")))
#else
#define VNXVIDEO_DECLSPEC
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
    typedef struct { void* ptr; } vnxvideo_manager_t;
    typedef struct { void* ptr; } vnxvideo_videosource_t;
    typedef struct { void* ptr; } vnxvideo_buffer_t;
    typedef struct { void* ptr; } vnxvideo_raw_sample_t;
    typedef struct { void* ptr; } vnxvideo_allocator_t;

    typedef struct { void* ptr; } vnxvideo_rawproc_t; // raw processor - a superclass
    typedef struct { void* ptr; } vnxvideo_h264_encoder_t;
    typedef struct { void* ptr; } vnxvideo_h264_source_t;
    typedef struct { void* ptr; } vnxvideo_media_source_t;
    typedef struct { void* ptr; } vnxvideo_composer_t;
    typedef struct { void* ptr; } vnxvideo_analytics_t; // video analysis
    typedef struct { void* ptr; } vnxvideo_imganalytics_t; // still image analysis, with no respect to timestamps and previous history
    typedef struct { void* ptr; } vnxvideo_rawtransform_t;

    typedef struct { void* ptr; } vnxvideo_decoder_t;
    typedef struct { void* ptr; } vnxvideo_renderer_t;
    typedef struct { void* ptr; } vnxvideo_transcoder_t;

    typedef struct { void* ptr; } vnxvideo_vmsplugin_t;
#pragma pack(pop)

    typedef enum { VNXLOG_NONE=0, VNXLOG_ERROR=1, VNXLOG_WARNING=2, VNXLOG_INFO=3, VNXLOG_DEBUG=4, VNXLOG_HIGHEST=10} ELogLevel;
    typedef void(*vnxvideo_log_t)(void* usrptr, ELogLevel level, const char* subsystem, const char* message);

    const int vnxvideo_err_ok = 0;
    const int vnxvideo_err_not_implemented = -1;
    const int vnxvideo_err_invalid_parameter = -2;
    const int vnxvideo_err_external_api = -3;

    // coded media subtypes
    typedef enum { 
        EMST_ANY = 0, 
        EMST_H264 = 1, 
        EMST_HEVC = 2, 
        EMST_PCMU = 16, // default RTP details: 8 bps 8000 samples per second
        EMST_PCMA = 32, // same as above
        EMST_OPUS = 64,
        EMST_AAC = 128,
        EMST_LPCM = 256,
    } EMediaSubtype;

    // a few supported media formats
    // https://msdn.microsoft.com/en-us/library/ms867704.aspx
    // http://www.fourcc.org/yuv.php
    typedef enum { EMF_NONE = 0, 
        // supported by encoder and IRawProc w/o conversion:
        EMF_I420, EMF_YV12, EMF_NV12, EMF_NV21, 
        // unsupported w/o conversion:
        EMF_YUY2, EMF_UYVY, EMF_YVU9, 
        EMF_RGB32, EMF_RGB24, EMF_RGB16 // RGB formats are assumed packed here
        , EMF_I444 // YUV planar w/o chroma subsampling
        , EMF_P422 // YUV planar 4:2:2. Could not find appropriate FOURCC
        , EMF_P440 // YUV planar 4:4:0, meaning that all chroma readings are taken from 1st line
        , EMF_GRAY // YUV 4:0:0

        , EMF_LPCM = 32, // default 16 bps signed le
    } ERawMediaFormat;
    typedef ERawMediaFormat EColorspace; // backwards compatibility

    VNXVIDEO_DECLSPEC bool vnxvideo_emf_is_video(ERawMediaFormat emf);
    VNXVIDEO_DECLSPEC bool vnxvideo_emf_is_audio(ERawMediaFormat emf);

    typedef int(*vnxvideo_on_frame_format_t)(void* usrptr, ERawMediaFormat emf, int width, int height); 
    // ^ args: width and height of luma plane for video, sample rate and number of channels for audio
    typedef int(*vnxvideo_on_buffer_t)(void* usrptr, vnxvideo_buffer_t buffer, uint64_t timestamp);
    typedef int(*vnxvideo_on_raw_sample_t)(void* usrptr, vnxvideo_raw_sample_t buffer, uint64_t timestamp);
    typedef int(*vnxvideo_on_json_t)(void* usrptr, const char* json_buffer, int json_buffer_size, uint64_t timestamp);

    typedef void(*vnxvideo_action_t)(void* usrptr);

    // call only once. second call will be ignored with invalid paramteter error code
    VNXVIDEO_DECLSPEC int vnxvideo_init(vnxvideo_log_t log_handler, void* usrptr, ELogLevel max_level);

    VNXVIDEO_DECLSPEC int vnxvideo_manager_dshow_create(vnxvideo_manager_t* mgr);
    VNXVIDEO_DECLSPEC int vnxvideo_manager_v4l_create(vnxvideo_manager_t* mgr);
    VNXVIDEO_DECLSPEC void vnxvideo_manager_free(vnxvideo_manager_t);

    VNXVIDEO_DECLSPEC int vnxvideo_enumerate_video_sources(vnxvideo_manager_t mgr, int details, char* json_buffer, int buffer_size);

    VNXVIDEO_DECLSPEC int vnxvideo_video_source_create(vnxvideo_manager_t mgr, const char* address, const char* mode,
        vnxvideo_videosource_t* out);
    VNXVIDEO_DECLSPEC void vnxvideo_video_source_free(vnxvideo_videosource_t);

    VNXVIDEO_DECLSPEC int vnxvideo_video_source_subscribe(vnxvideo_videosource_t source, 
        vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
        vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data);
    VNXVIDEO_DECLSPEC int vnxvideo_video_source_start(vnxvideo_videosource_t);
    VNXVIDEO_DECLSPEC int vnxvideo_video_source_stop(vnxvideo_videosource_t);

    VNXVIDEO_DECLSPEC int vnxvideo_buffer_dup(vnxvideo_buffer_t, vnxvideo_buffer_t*);
    VNXVIDEO_DECLSPEC void vnxvideo_buffer_free(vnxvideo_buffer_t);
    VNXVIDEO_DECLSPEC int vnxvideo_buffer_get_data(vnxvideo_buffer_t, uint8_t* *data, int* size);
    // create a deep copy of a buffer, i.e. copy the data into a new buffer owned by this buffer_t instance.
    VNXVIDEO_DECLSPEC int vnxvideo_buffer_copy(vnxvideo_buffer_t, vnxvideo_buffer_t*);
    // create a shallow wrapper from a memory buffer. it won't own the data buffer. the caller should somehow guarantee that
    // pointer "data" is valid during the lifetime of the created vnxvideo_buffer_t instance 
    // and all of its clones (that might be created via vnxvideo_buffer_dup call).
    // one may however use such shallow wrapper to create a deep copy of a vnxvideo_buffer_t using vnxvideo_buffer_copy. 
    // it won't share the same memory ("data") but will have its own buffer, and will automatically manage its lifetime.
    // note that vnxvideo_buffer_t instance created by vnxvideo_buffer_wrap should be freed, as any other vnxvideo_buffer_t.
    VNXVIDEO_DECLSPEC int vnxvideo_buffer_wrap(const uint8_t *data, int size, vnxvideo_buffer_t*);
    // copy memory and wrap it into vnxvideo buffer in a single call
    VNXVIDEO_DECLSPEC int vnxvideo_buffer_copy_wrap(const uint8_t *data, int size, vnxvideo_buffer_t*);

    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_dup(vnxvideo_raw_sample_t, vnxvideo_raw_sample_t*); // shallow copy (share same data w/original)
    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_copy(vnxvideo_raw_sample_t, vnxvideo_raw_sample_t*); // deep copy
    VNXVIDEO_DECLSPEC void vnxvideo_raw_sample_free(vnxvideo_raw_sample_t);
    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_get_format(vnxvideo_raw_sample_t, EColorspace *csp, int *width, int *height);
    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_get_data(vnxvideo_raw_sample_t, int* strides, uint8_t **planes); // pass 4-elemets arrays here
                                                                                                               // create a shallow raw_sample wrapper over the memory buffer(s)
                                                                                                               // like vnxvideo_buffer_wrap
    // allocate a new raw sample of specified format
    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_allocate(EColorspace csp, int width, int height, vnxvideo_raw_sample_t*);
    // create a shallow raw_sample wrapper over the memory buffer(s)
    // like vnxvideo_buffer_wrap
    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_wrap(EColorspace csp, int width, int height, 
        int* strides, uint8_t **planes, vnxvideo_raw_sample_t*);
    // create a raw sample from a bmp containing 16,24 or 32-bit RGB DIB. sample should be freed when no longer needed
    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_from_bmp(const uint8_t* data, int size, vnxvideo_raw_sample_t* sample);
    // select a ROI on a sample, sharing the same underlying memory
    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_select_roi(vnxvideo_raw_sample_t in,
        int roi_left, int roi_top, int roi_width, int roi_height,
        vnxvideo_raw_sample_t* out);
    // crop and then downscale a sample
    VNXVIDEO_DECLSPEC int vnxvideo_raw_sample_crop_resize(vnxvideo_raw_sample_t in,
        int roi_left, int roi_top, int roi_width, int roi_height, 
        int target_width, int target_height, 
        vnxvideo_raw_sample_t* out);


    VNXVIDEO_DECLSPEC void vnxvideo_rawproc_free(vnxvideo_rawproc_t proc);
    VNXVIDEO_DECLSPEC int vnxvideo_rawproc_set_format(vnxvideo_rawproc_t proc, EColorspace csp, int width, int height);
    VNXVIDEO_DECLSPEC int vnxvideo_rawproc_process(vnxvideo_rawproc_t proc, vnxvideo_raw_sample_t sample, uint64_t timestamp);
    VNXVIDEO_DECLSPEC int vnxvideo_rawproc_flush(vnxvideo_rawproc_t proc);


    VNXVIDEO_DECLSPEC int vnxvideo_h264_encoder_create(const char* json_config, vnxvideo_h264_encoder_t* encoder);
    VNXVIDEO_DECLSPEC vnxvideo_rawproc_t vnxvideo_h264_encoder_to_rawproc(vnxvideo_h264_encoder_t); // cast, not duplication
    VNXVIDEO_DECLSPEC int vnxvideo_h264_encoder_subscribe(vnxvideo_h264_encoder_t encoder, 
        vnxvideo_on_buffer_t handle_data, void* usrptr); // each data buffer is a NAL unit

    VNXVIDEO_DECLSPEC int vnxvideo_composer_create(const char* json_config, vnxvideo_composer_t* composer);
    VNXVIDEO_DECLSPEC vnxvideo_rawproc_t vnxvideo_composer_to_rawproc(vnxvideo_composer_t); // cast, not duplication
    VNXVIDEO_DECLSPEC int vnxvideo_composer_set_overlay(vnxvideo_composer_t composer, vnxvideo_raw_sample_t image);

    VNXVIDEO_DECLSPEC int vnxvideo_analytics_create(const char* json_config, vnxvideo_analytics_t* analytics);
    VNXVIDEO_DECLSPEC vnxvideo_rawproc_t vnxvideo_analytics_to_rawproc(vnxvideo_analytics_t); // cast, not duplication
    VNXVIDEO_DECLSPEC int vnxvideo_analytics_subscribe(vnxvideo_analytics_t analytics, 
        vnxvideo_on_json_t handle_json, void* usrptr_json, // json for rare events
        vnxvideo_on_buffer_t handle_binary, void* usrptr_binary); // binary buffer for "metadata" (like tracking)

    VNXVIDEO_DECLSPEC int vnxvideo_imganalytics_create(const char* json_config, vnxvideo_imganalytics_t *ian);
    VNXVIDEO_DECLSPEC void vnxvideo_imganalytics_free(vnxvideo_imganalytics_t ian);
    VNXVIDEO_DECLSPEC int vnxvideo_imganalytics_set_format(vnxvideo_imganalytics_t ian, EColorspace csp, int width, int height);
    VNXVIDEO_DECLSPEC int vnxvideo_imganalytics_process(vnxvideo_imganalytics_t ian, vnxvideo_raw_sample_t sample,
        char* /*out*/json_buffer, int* /*inout*/ buffer_size);


    VNXVIDEO_DECLSPEC int vnxvideo_rawtransform_create(const char* json_config, vnxvideo_rawtransform_t* transform);
    VNXVIDEO_DECLSPEC vnxvideo_rawproc_t vnxvideo_rawtransform_to_rawproc(vnxvideo_rawtransform_t); // cast, not duplication
    VNXVIDEO_DECLSPEC int vnxvideo_rawtransform_subscribe(vnxvideo_rawtransform_t transform, 
        vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
        vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data);

    VNXVIDEO_DECLSPEC int vnxvideo_h264_decoder_create(vnxvideo_decoder_t* decoder);
    VNXVIDEO_DECLSPEC int vnxvideo_hevc_decoder_create(vnxvideo_decoder_t* decoder);
    VNXVIDEO_DECLSPEC void vnxvideo_decoder_free(vnxvideo_decoder_t decoder);
    VNXVIDEO_DECLSPEC int vnxvideo_decoder_subscribe(vnxvideo_decoder_t decoder,
        vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
        vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data);
    VNXVIDEO_DECLSPEC int vnxvideo_decoder_decode(vnxvideo_decoder_t decoder, vnxvideo_buffer_t buffer, uint64_t timestamp);
    VNXVIDEO_DECLSPEC int vnxvideo_decoder_flush(vnxvideo_decoder_t decoder);

    VNXVIDEO_DECLSPEC int vnxvideo_renderer_create(int refresh_rate, vnxvideo_renderer_t* renderer);
    VNXVIDEO_DECLSPEC vnxvideo_videosource_t vnxvideo_renderer_to_videosource(vnxvideo_renderer_t); // cast, not duplication
    // all the inputs (rawproc objects) created by the next function should not be used after the parent renderer is destroyed.
    // specifically, they should be unsubscribed from video sources before the renderer is destroyed.
    VNXVIDEO_DECLSPEC int vnxvideo_renderer_create_input(vnxvideo_renderer_t renderer, int index, const char* transform_json, 
        vnxvideo_rawproc_t* input);
    VNXVIDEO_DECLSPEC int vnxvideo_renderer_update_layout(vnxvideo_renderer_t renderer, 
        int width, int height, uint8_t* backgroundColor, vnxvideo_raw_sample_t backgroundImage, 
        vnxvideo_raw_sample_t nosignalImage, const char* layout);
    VNXVIDEO_DECLSPEC int vnxvideo_renderer_set_background(vnxvideo_renderer_t renderer, 
        uint8_t* backgroundColor, vnxvideo_raw_sample_t backgroundImage);
    VNXVIDEO_DECLSPEC int vnxvideo_renderer_set_nosignal(vnxvideo_renderer_t renderer, vnxvideo_raw_sample_t nosignalImage);

    VNXVIDEO_DECLSPEC int vnxvideo_with_shm_allocator_str(const char* name, int maxSizeMB, vnxvideo_action_t action, void* usrptr);
    VNXVIDEO_DECLSPEC int vnxvideo_with_shm_allocator_ptr(vnxvideo_allocator_t allocator, vnxvideo_action_t action, void* usrptr);
    VNXVIDEO_DECLSPEC void vnxvideo_shm_allocator_duplicate(vnxvideo_allocator_t* allocator);
    VNXVIDEO_DECLSPEC void vnxvideo_shm_allocator_free(vnxvideo_allocator_t allocator);

    VNXVIDEO_DECLSPEC int vnxvideo_local_client_create(const char* name, vnxvideo_videosource_t* out);
    VNXVIDEO_DECLSPEC int vnxvideo_local_server_create(const char* name, int maxSizeMB, vnxvideo_rawproc_t* out);

    // to be deprecated {{
    typedef int (*vnxvideo_h264_source_create_t)(const char* json_config, vnxvideo_h264_source_t* source);
    VNXVIDEO_DECLSPEC int vnxvideo_h264_source_subscribe(vnxvideo_h264_source_t source,
        vnxvideo_on_buffer_t handle_data, void* usrptr); // each data buffer is a NAL unit
    VNXVIDEO_DECLSPEC int vnxvideo_h264_source_events_subscribe(vnxvideo_h264_source_t source,
        vnxvideo_on_json_t handle_event, void* usrptr); // each buffer is a JSON value
    VNXVIDEO_DECLSPEC void vnxvideo_h264_source_free(vnxvideo_h264_source_t source);
    VNXVIDEO_DECLSPEC int vnxvideo_h264_source_start(vnxvideo_h264_source_t source);
    VNXVIDEO_DECLSPEC int vnxvideo_h264_source_stop(vnxvideo_h264_source_t source);
    // }}

    // Coded media source. One source may actually represent/output several streams of different media types.
    // This is only an interface, it's up to users and implementers to decide how to use this convention.
    typedef int (*vnxvideo_media_source_create_t)(const char* json_config, vnxvideo_media_source_t* source);
    VNXVIDEO_DECLSPEC int vnxvideo_media_source_subscribe(vnxvideo_media_source_t source,
        EMediaSubtype media_subtype, vnxvideo_on_buffer_t handle_data, void* usrptr);
    VNXVIDEO_DECLSPEC int vnxvideo_media_source_events_subscribe(vnxvideo_media_source_t source,
        vnxvideo_on_json_t handle_event, void* usrptr); // each buffer is JSON value
    VNXVIDEO_DECLSPEC void vnxvideo_media_source_free(vnxvideo_media_source_t source);
    VNXVIDEO_DECLSPEC int vnxvideo_media_source_start(vnxvideo_media_source_t source);
    VNXVIDEO_DECLSPEC int vnxvideo_media_source_stop(vnxvideo_media_source_t source);
    // Enumerate media(sub)types produced by this media source.
    VNXVIDEO_DECLSPEC int vnxvideo_media_source_enum_mediatypes(vnxvideo_media_source_t source,
        EMediaSubtype* buffer, int buffer_size_bytes, int* count);
    // Get extradata for a stream of a given media subtype.
    // Extradata is a set of stream/encoder/source parameters or config, 
    // sometimes repeated in-band (like VPS/SPS/PPS for H.26[45]),
    // but sometimes not (like AudioSpecificConfig data for AAC).
    // Extradata should be returned in FFmpeg-like format, that is body of avcC/hvcC 
    // mp4 boxes for H264/H265 video, or 14496-3-encoded AudioSpecificConfig for AAC. 
    VNXVIDEO_DECLSPEC int vnxvideo_media_source_get_extradata(vnxvideo_media_source_t source,
        EMediaSubtype media_subtype, vnxvideo_buffer_t* buffer); // may return NULL as buffer->ptr

    // wrap a media source to have interface of h264 video source and vice versa.
    // ownership of object passed as an argument will be owned by resulting object.
    VNXVIDEO_DECLSPEC vnxvideo_h264_source_t  vnxvideo_media_source_to_h264(vnxvideo_media_source_t);
    VNXVIDEO_DECLSPEC vnxvideo_media_source_t vnxvideo_h264_source_to_media(vnxvideo_h264_source_t);

    VNXVIDEO_DECLSPEC int vnxvideo_create_audio_transcoder(int channels,
        EMediaSubtype input, const char* inputDetails,
        EMediaSubtype output, const char* outputDetails,
        vnxvideo_transcoder_t* transcoder);
    VNXVIDEO_DECLSPEC void vnxvideo_transcoder_free(vnxvideo_transcoder_t transcoder);
    VNXVIDEO_DECLSPEC int vnxvideo_transcoder_subscribe(vnxvideo_transcoder_t transcoder,
        vnxvideo_on_buffer_t handle_data, void* usrptr);
    VNXVIDEO_DECLSPEC int vnxvideo_transcoder_process(vnxvideo_transcoder_t transcoder, vnxvideo_buffer_t buffer, uint64_t timestamp);

    typedef int (*vnxvideo_vmsplugin_create_t)(const char* json_config, vnxvideo_vmsplugin_t* vmsplugin);
    VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_free(vnxvideo_vmsplugin_t vmsplugin);
    VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_h264_source_create_live(vnxvideo_vmsplugin_t vmsplugin,
        const char* channel_selector, vnxvideo_h264_source_t* source);
    VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_h264_source_create_archive(vnxvideo_vmsplugin_t vmsplugin,
        const char* channel_selector, uint64_t begin, uint64_t end, double speed,
        vnxvideo_h264_source_t* source);
    VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_get_archive_timeline(vnxvideo_vmsplugin_t vmsplugin,
        const char* channel_selector, uint64_t begin, uint64_t end,
        vnxvideo_buffer_t* intervals); // out buffer holds a flat array of pairs of uint64_t, unmarshalled (in native byte order)
    VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_get_snapshot(vnxvideo_vmsplugin_t vmsplugin,
        const char* channel_selector, uint64_t timestamp, vnxvideo_buffer_t* jpeg);

#ifdef __cplusplus
}
#endif
