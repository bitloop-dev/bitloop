#include <bitloop/core/capture_manager.h>
#include <bitloop/util/math_util.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <algorithm>

BL_BEGIN_NS;


bool webp_snapshot_encode_rgba(
    const uint8_t* rgba,
    int w, int h,
    float quality,
    int method,
    bool lossless,
    int near_lossless,
    int alpha_quality,
    std::vector<uint8_t>& out)
{
    WebPConfig cfg;
    if (!WebPConfigInit(&cfg)) return false;

    if (lossless)
    {
        cfg.lossless = 1;
        WebPConfigLosslessPreset(&cfg, 9);
        cfg.quality = 100.0f;
        cfg.near_lossless = near_lossless;
        cfg.exact = 1;
        cfg.thread_level = 1;
        cfg.preprocessing = 0;
        cfg.alpha_quality = 0;
    }
    else {
        cfg.lossless = 0;
        cfg.quality = quality; // 0..100
        cfg.method = method;   // 0..6
        cfg.alpha_quality = alpha_quality;
        cfg.thread_level = 1;
    }

    if (!WebPValidateConfig(&cfg)) return false;

    WebPPicture pic;
    if (!WebPPictureInit(&pic)) return false;
    pic.width = w;
    pic.height = h;
    pic.use_argb = 1; // required for lossless

    if (!WebPPictureImportRGBA(&pic, rgba, w * 4)) {
        WebPPictureFree(&pic);
        return false;
    }

    WebPMemoryWriter wrt;
    WebPMemoryWriterInit(&wrt);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &wrt;

    const int ok = WebPEncode(&cfg, &pic);
    if (!ok) {
        WebPMemoryWriterClear(&wrt);
        WebPPictureFree(&pic);
        return false;
    }

    out.assign(wrt.mem, wrt.mem + wrt.size);
    WebPMemoryWriterClear(&wrt);
    WebPPictureFree(&pic);
    return true;
}

bool webp_anim_add_rgba(
    WebPAnimEncoder* enc,
    const uint8_t* rgba, int w, int h, int stride_bytes,
    int timestamp_ms,
    bool lossless,
    int near_lossless,
    float quality,
    int method)
{
    WebPConfig cfg;
    if (!WebPConfigInit(&cfg)) return false;

    if (lossless) {
        cfg.lossless = 1;
        WebPConfigLosslessPreset(&cfg, 9);
        cfg.quality = quality; // effort in lossless
        cfg.near_lossless = near_lossless;
        cfg.exact = 1; // preserve RGB under alpha
        cfg.alpha_quality = 0;
        cfg.thread_level = 1;
        cfg.preprocessing = 0;
        cfg.image_hint = WEBP_HINT_GRAPH;
    }
    else {
        cfg.lossless = 0;
        cfg.quality = quality; // 0..100
        cfg.method = method; // 0..6
        cfg.alpha_quality = 100;
        cfg.thread_level = 1;
    }

    if (!WebPValidateConfig(&cfg)) return false;

    WebPPicture pic;
    if (!WebPPictureInit(&pic)) return false;
    pic.width = w;
    pic.height = h;
    pic.use_argb = 1;

    if (!WebPPictureImportRGBA(&pic, rgba, stride_bytes)) {
        WebPPictureFree(&pic);
        return false;
    }

    const int ok = WebPAnimEncoderAdd(enc, &pic, timestamp_ms, &cfg);
    WebPPictureFree(&pic);
    return ok != 0;
}

bool webp_anim_encode_simple(
    const std::vector<bytebuf>& frames_rgba,
    int w, int h, int stride_bytes,
    int delays_ms,
    bool lossless, int near_lossless, float quality, int method,
    int loop_count,
    bytebuf& out_bytes)
{
    if (frames_rgba.empty()) return false;

    WebPAnimEncoderOptions opts;
    WebPAnimEncoderOptionsInit(&opts);
    opts.anim_params.bgcolor = 0x00000000; // RGBA
    opts.anim_params.loop_count = loop_count; // 0 = loop forever
    
    WebPAnimEncoder* enc = WebPAnimEncoderNew(w, h, &opts);
    if (!enc) return false;

    int t = 0;
    for (size_t i = 0; i < frames_rgba.size(); ++i) {
        if (!webp_anim_add_rgba(enc, frames_rgba[i].data(), w, h, stride_bytes, t,
            lossless, near_lossless, quality, method)) {
            WebPAnimEncoderDelete(enc);
            return false;
        }
        t += delays_ms;
    }

    // flush
    if (!WebPAnimEncoderAdd(enc, nullptr, t, nullptr)) {
        WebPAnimEncoderDelete(enc);
        return false;
    }

    WebPData webp = { 0 };
    const int ok = WebPAnimEncoderAssemble(enc, &webp);
    WebPAnimEncoderDelete(enc);
    if (!ok) return false;

    out_bytes.assign(webp.bytes, webp.bytes + webp.size);
    WebPDataClear(&webp);
    return true;
}

std::string xml_escape(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:   out += c;        break;
        }
    }
    return out;
}

std::string make_xmp_packet_with_save(std::string_view save_text)
{
    const std::string escaped = xml_escape(save_text);

    std::string xmp;
    xmp.reserve(300 + escaped.size());
    xmp += "<?xpacket begin='\xEF\xBB\xBF' id='W5M0MpCehiHzreSzNTczkc9d'?>\n";
    xmp += "<x:xmpmeta xmlns:x='adobe:ns:meta/'>\n";
    xmp += "  <rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>\n";
    xmp += "    <rdf:Description xmlns:bl='https://bitloop.dev/ns/1.0/'>\n";
    xmp += "      <bl:save>";
    xmp += escaped;
    xmp += "</bl:save>\n";
    xmp += "    </rdf:Description>\n";
    xmp += "  </rdf:RDF>\n";
    xmp += "</x:xmpmeta>\n";
    xmp += "<?xpacket end='w'?>\n";
    return xmp;
}

bool webp_set_chunk_inplace(bytebuf& io_webp,
   const char fourcc[4],
   const uint8_t* payload,
   size_t payload_size)
{
    if (io_webp.empty()) return false;

    WebPData webp_data{ io_webp.data(), io_webp.size() };
    WebPMux* mux = WebPMuxCreate(&webp_data, 1 /* copy_data */);
    if (!mux) return false;

    WebPData chunk{ payload, payload_size };
    const WebPMuxError set_err = WebPMuxSetChunk(mux, fourcc, &chunk, 1 /* copy_data */);
    if (set_err != WEBP_MUX_OK) { WebPMuxDelete(mux); return false; }

    WebPData out_data{ nullptr, 0 };
    const WebPMuxError asm_err = WebPMuxAssemble(mux, &out_data);
    WebPMuxDelete(mux);

    if (asm_err != WEBP_MUX_OK || !out_data.bytes || out_data.size == 0) {
        WebPDataClear(&out_data);
        return false;
    }

    io_webp.assign(out_data.bytes, out_data.bytes + out_data.size);
    WebPDataClear(&out_data);
    return true;
}

bool webp_set_save_string_as_xmp_inplace(bytebuf& io_webp, std::string_view save_text)
{
    const std::string xmp = make_xmp_packet_with_save(save_text);
    static constexpr char kXMP[4] = { 'X','M','P',' ' };
    return webp_set_chunk_inplace(io_webp, kXMP,
        reinterpret_cast<const uint8_t*>(xmp.data()),
        xmp.size());
}

bool webp_extract_save_from_xmp(const bytebuf& webp, std::string& out_save)
{
    if (webp.empty()) return false;

    WebPData webp_data{ webp.data(), webp.size() };
    WebPMux* mux = WebPMuxCreate(&webp_data, 1 /* copy_data */);
    if (!mux) return false;

    WebPData chunk{ nullptr, 0 };
    const WebPMuxError err = WebPMuxGetChunk(mux, "XMP ", &chunk);
    if (err != WEBP_MUX_OK || !chunk.bytes || chunk.size == 0) {
        WebPMuxDelete(mux);
        return false;
    }

    // Copy while mux is alive
    const std::string xmp(reinterpret_cast<const char*>(chunk.bytes), chunk.size);

    WebPMuxDelete(mux);

    const std::string open = "<bl:save>";
    const std::string close = "</bl:save>";

    const size_t a = xmp.find(open);
    if (a == std::string::npos) return false;

    const size_t b = xmp.find(close, a + open.size());
    if (b == std::string::npos) return false;

    out_save = xmp.substr(a + open.size(), b - (a + open.size()));
    return true;
}


// ────── FFmpegWorker ──────

#if BITLOOP_FFMPEG_ENABLED
static bool is_h264(const AVCodec* c) { return c && c->id == AV_CODEC_ID_H264; }
static bool is_h265(const AVCodec* c) { return c && c->id == AV_CODEC_ID_HEVC; }
static bool h264_frame_size_supported(int w, int h)
{
    const int mb = ((w + 15) / 16) * ((h + 15) / 16);
    return mb <= 36864; // Level 5.2 macroblock bound (approx)
}

static bool g_want_hevc_10bit = false;
static enum AVPixelFormat SelectPixelFormat(AVCodecContext*, const enum AVPixelFormat* fmts)
{
    // Prefer 10-bit if requested, otherwise 8-bit 4:2:0
    if (g_want_hevc_10bit) {
        for (const enum AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
            if (*p == AV_PIX_FMT_YUV420P10LE) return AV_PIX_FMT_YUV420P10LE;
    }
    for (const enum AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_YUV420P) return AV_PIX_FMT_YUV420P;

    // Fallback to the first offered format
    return fmts[0];
}

void FFmpegWorker::process(CaptureManager* capture_manager, CaptureConfig video_config)
{
    config = video_config;

    if (!startCapture())
        return;

    while (true)
    {
        if (!capture_manager->waitForWorkAvailable())
            break; // recording stopped, nothing left to do

        if (!capture_manager->isBusy())
        {
            // If finalize was requested and no frame is pending, flush and exit
            if (capture_manager->shouldFinalize() )
            {
                finalize(capture_manager);
                capture_manager->clearFinalizeRequest();
                break;
            }
        }
        else
        {
            // Grab the pending frame
            EncodeFrame frame = capture_manager->takePendingFrame();

            if (frame.empty())
            {
                // Shouldn't occur, but if it does, check we're still recording and skip frame
                if (!capture_manager->isRecording()) break;
                continue;
            }

            encodeFrame(frame);

            if (finalizing)
                finalize(capture_manager);

            // Tell capture_manager we're ready for the next frame
            capture_manager->markEncoderIdle();
        }

        if (capture_manager->shouldFinalize())
        {
            finalize(capture_manager);
            capture_manager->clearFinalizeRequest();
            break;
        }
    }
}

bool FFmpegWorker::startCapture()
{
    const char* filename = config.filename.c_str();
    trimmed_resolution = (config.resolution / 2) * 2;

    // Allocate format context
    if (avformat_alloc_output_context2(&format_context, nullptr, nullptr, filename) < 0)
    {
        blPrint() << "ERROR: avformat_alloc_output_context2(...)";
        return false;
    }

    // Find H.264 codec
    //const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    const AVCodec* codec = nullptr;

    #if BITLOOP_FFMPEG_X265_ENABLED
    if (config.format == CaptureFormat::x265)
        codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
    else
    #endif
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);

    // Avoid H.264 at 8K
    if (is_h264(codec) && !h264_frame_size_supported(trimmed_resolution.x, trimmed_resolution.y))
    {
        blPrint() << "ERROR: H.264 does not support resolution "
                  << trimmed_resolution.x << "x" << trimmed_resolution.y
                  << " at the selected level. Choose H.265 or reduce resolution.";
        return false;
    }

    if (!codec) { blPrint() << "ERROR: avcodec_find_encoder(...)"; return false; }

    // Create video stream
    stream = avformat_new_stream(format_context, codec);
    if (!stream) { blPrint() << "ERROR: avformat_new_stream(...)"; return false; }

    // Allocate codec context
    codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) { blPrint() << "ERROR: avcodec_alloc_context3(...)"; return false; }

    // If muxer wants global headers, set flag
    if (format_context->oformat->flags & AVFMT_GLOBALHEADER)
        codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Set codec parameters
    codec_context->bit_rate = config.bitrate;
    codec_context->width = trimmed_resolution.x;
    codec_context->height = trimmed_resolution.y;
    codec_context->time_base = { 1, config.fps };
    codec_context->framerate = { config.fps, 1 };
    codec_context->gop_size = config.fps * 2; // Group of pictures
    codec_context->max_b_frames = is_h265(codec) ? 6 : 3;;
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;

    codec_context->get_format = &SelectPixelFormat;
    ///const bool got_10bit = (codec_context->pix_fmt == AV_PIX_FMT_YUV420P10LE);

    // Set preset for quality/speed tradeoff
    if (is_h265(codec))
    {
        // Prefer 10-bit for quality (if requested and build supports it)
        av_opt_set(codec_context->priv_data, "profile", g_want_hevc_10bit ? "main10" : "main", 0);
        av_opt_set(codec_context->priv_data, "preset", "medium", 0);
    }
    else {
        // H.264 (8-bit typical unless you use a 10-bit x264 build)
        av_opt_set(codec_context->priv_data, "profile", "high", 0);
        av_opt_set(codec_context->priv_data, "preset", "veryslow", 0);
    }

    // VBV to constrain peaks (keeps muxers/players happy)
    av_opt_set_int(codec_context->priv_data, "maxrate", codec_context->bit_rate, 0);
    av_opt_set_int(codec_context->priv_data, "bufsize", codec_context->bit_rate / 2, 0);

    ///av_opt_set(codec_context->priv_data, "preset", "veryslow", 0);
    ///av_opt_set(codec_context->priv_data, "profile", "high", 0);

    // Open codec
    if (avcodec_open2(codec_context, codec, nullptr) < 0)
    {
        blPrint() << "ERROR: avcodec_open2(...)";
        return false;
    }

    // Associate codec parameters with stream
    if (avcodec_parameters_from_context(stream->codecpar, codec_context) < 0)
    {
        blPrint() << "ERROR: avcodec_parameters_from_context(...)";
        return false;
    }

    stream->time_base = codec_context->time_base;

    // Open output file
    if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&format_context->pb, filename, AVIO_FLAG_WRITE) < 0)
        {
            blPrint() << "ERROR: avio_open(...)";
            return false;
        }
    }

    // Write the stream header
    if (avformat_write_header(format_context, nullptr) < 0)
    {
        blPrint() << "ERROR: avformat_write_header(...)";
        return false;
    }

    // Allocate frame for YUV420P
    yuv_frame = av_frame_alloc();
    if (!yuv_frame) { blPrint() << "ERROR: av_frame_alloc()"; return false; }
    yuv_frame->format = codec_context->pix_fmt;
    yuv_frame->width = trimmed_resolution.x;
    yuv_frame->height = trimmed_resolution.y;
    if (av_frame_get_buffer(yuv_frame, 32) < 0) { blPrint() << "ERROR: av_frame_get_buffer()"; return false; }

    // Allocate frame for RGB
    rgb_frame = av_frame_alloc();
    if (!rgb_frame) { blPrint() << "ERROR: av_frame_alloc()"; return false; }

    rgb_frame->format = AV_PIX_FMT_RGBA;
    rgb_frame->width = config.resolution.x;
    rgb_frame->height = config.resolution.y;
    if (av_frame_get_buffer(rgb_frame, 32) < 0)
    {
        blPrint() << "ERROR: av_frame_get_buffer(...)";
        return false;
    }

    // Initialize scaling context
    sws_ctx = sws_getContext(
        config.resolution.x, config.resolution.y, AV_PIX_FMT_RGBA,           // src format
        codec_context->width, codec_context->height, codec_context->pix_fmt, // dest format
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_ctx) { blPrint() << "ERROR: sws_getContext(...)"; return false; }

    // Allocate packet
    packet = av_packet_alloc();
    if (!packet) { blPrint() << "ERROR: av_packet_alloc()"; return false; }

    frame_index = 0;
    return true;
}

bool FFmpegWorker::encodeFrame(EncodeFrame& frame)
{
    av_frame_make_writable(rgb_frame);

    int src_line_size = config.resolution.x * 4;
    int targ_line_size = rgb_frame->linesize[0];

    uint8_t* data = frame.frameData();
    const uint8_t* src_row = nullptr;
    uint8_t* targ_row = nullptr;

    //if (config.flip)
    //{
    //    int targ_y = 0;
    //    for (int src_y = trimmed_resolution.y - 1; src_y >= 0; src_y--)
    //    {
    //        // Copy row
    //        src_row = data + (src_y * src_line_size);
    //        targ_row = rgb_frame->data[0] + (targ_y * targ_line_size);
    //
    //        memcpy(targ_row, src_row, targ_line_size);
    //
    //        targ_y++;
    //    }
    //}
    //else
    {
        for (int src_y = 0; src_y < trimmed_resolution.y; src_y++)
        {
            // Copy row
            src_row = data + (src_y * src_line_size);
            targ_row = rgb_frame->data[0] + (src_y * targ_line_size);

            memcpy(targ_row, src_row, targ_line_size);
        }
    }

    // Convert RGB frame to YUV420P
    sws_scale(sws_ctx,
        rgb_frame->data, rgb_frame->linesize,   // Source frame data
        0, config.resolution.y,                 // Source frame height
        yuv_frame->data, yuv_frame->linesize);  // Output frame data

    blPrint() << "<<Encoding Frame>> " << frame_index;
    yuv_frame->pts = frame_index++;

    if (avcodec_send_frame(codec_context, yuv_frame) < 0)
        return false;

    while (avcodec_receive_packet(codec_context, packet) == 0)
    {
        av_packet_rescale_ts(packet, codec_context->time_base, stream->time_base);
        packet->stream_index = stream->index;

        if (av_interleaved_write_frame(format_context, packet) < 0)
            return false;

        av_packet_unref(packet);
    }



    return true;
}

bool FFmpegWorker::finalize(CaptureManager* capture_manager)
{
    blPrint() << "<<Finalizing>>";

    // Flush the encoder
    avcodec_send_frame(codec_context, nullptr);
    while (avcodec_receive_packet(codec_context, packet) == 0)
    {
        av_packet_rescale_ts(packet, codec_context->time_base, stream->time_base);
        packet->stream_index = stream->index;
        av_interleaved_write_frame(format_context, packet);
        av_packet_unref(packet);
    }

    // Write trailer
    av_write_trailer(format_context);

    // Free resources
    sws_freeContext(sws_ctx);
    av_frame_free(&yuv_frame);
    av_frame_free(&rgb_frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    avio_close(format_context->pb);
    avformat_free_context(format_context);

    capture_manager->onFinalized(false);

    return true;
}
#endif

// ────── WebPWorker ──────

void WebPWorker::process(CaptureManager* capture_manager, CaptureConfig capture_config)
{
    config = capture_config;

    if (!startCapture())
        return;

    blPrint() << "WebPWorker::process main loop...";
    while (true)
    {
        if (!capture_manager->waitForWorkAvailable())
            break; // recording stopped, nothing left to do

        blPrint() << "Found work...";

        if (!capture_manager->isBusy())
        {
            // If finalize was requested and no frame is pending, flush and exit
            if (capture_manager->shouldFinalize())
            {
                finalize(capture_manager);
                capture_manager->clearFinalizeRequest();
                break;
            }
        }
        else
        {
            blPrint() << "Grabbing pending frame...";

            // Grab the pending frame
            EncodeFrame frame = capture_manager->takePendingFrame();

            if (frame.empty())
            {
                blPrint() << "frame is empty";
                capture_manager->onFinalized(true);
                break;

                // Shouldn't occur, but if it does, check we're still recording and skip frame
                if (!capture_manager->isRecording() && !capture_manager->isSnapshotting()) break;
                    continue;
            }

            encodeFrame(frame);

            // Tell capture_manager we're ready for the next frame
            capture_manager->markEncoderIdle();

            if (config.format == CaptureFormat::WEBP_SNAPSHOT)
            {
                finalize(capture_manager);
                break;
            }
            else if (config.format == CaptureFormat::WEBP_VIDEO &&
                     config.record_frame_count != 0 && frame_index >= config.record_frame_count)
            {
                finalize(capture_manager);
                break;
            }
        }

        if (capture_manager->shouldFinalize())
        {
            finalize(capture_manager);
            break;
        }
    }
    blPrint() << "Thread exiting...";
}

bool WebPWorker::startCapture()
{
    frame_index = 0;
    //rgba_frames.clear();
    encoded_data.clear();

    if (config.format == CaptureFormat::WEBP_VIDEO)
    {
        frame_delay_ms = (config.fps > 0) ? (1000 / config.fps) : 100;
        timestamp_ms = 0;

        WebPAnimEncoderOptions opts;
        if (!WebPAnimEncoderOptionsInit(&opts))
            return false;

        opts.anim_params.bgcolor = 0x00000000;
        opts.anim_params.loop_count = 0;

        // speed > size
        opts.minimize_size = 0;
        opts.allow_mixed = 0;

        enc = WebPAnimEncoderNew(config.resolution.x, config.resolution.y, &opts);
        if (!enc)
            return false;
    }

    return true;
}

bool WebPWorker::encodeFrame(EncodeFrame& frame)
{
    blPrint() << "Encoding frame...";

    // Grab metadata from this frame, save on finalize
    config.save_payload.swap(frame.payload);

    if (config.format == CaptureFormat::WEBP_SNAPSHOT)
    {
        const bool ok = webp_snapshot_encode_rgba(
            frame.frameData(),
            config.resolution.x,
            config.resolution.y,
            100,
            6,
            config.lossless,
            config.near_lossless,
            100,
            encoded_data);

        if (!ok)
            return false;
    }
    else if (config.format == CaptureFormat::WEBP_VIDEO)
    {
        if (!enc)
            return false;

        const bool ok = webp_anim_add_rgba(
            enc,
            frame.frameData(),
            config.resolution.x,
            config.resolution.y,
            config.resolution.x * 4,
            timestamp_ms,
            config.lossless,
            config.near_lossless,
            config.quality,
            6
        );

        if (!ok)
            return false;

        timestamp_ms += frame_delay_ms;
        frame_index++;
    }

    blPrint() << "Frame encoded";

    return true;
}

bool WebPWorker::finalize(CaptureManager* capture_manager)
{
    blPrint() << "<<Finalizing>>";

    if (config.format == CaptureFormat::WEBP_SNAPSHOT)
    {
        if (!config.save_payload.empty())
            webp_set_save_string_as_xmp_inplace(encoded_data, config.save_payload);

        std::lock_guard<std::mutex> lock(capture_manager->encoded_data_mutex);
        capture_manager->encoded_data = std::move(encoded_data);
        capture_manager->capture_to_memory_complete.store(true, std::memory_order_release);
    }
    else if (config.format == CaptureFormat::WEBP_VIDEO)
    {
        if (!enc)
            return false;

        // flush: last frame duration is set by the final timestamp
        if (!WebPAnimEncoderAdd(enc, nullptr, timestamp_ms, nullptr))
        {
            WebPAnimEncoderDelete(enc);
            enc = nullptr;
            return false;
        }

        WebPData webp = { 0 };
        if (!WebPAnimEncoderAssemble(enc, &webp))
        {
            WebPAnimEncoderDelete(enc);
            enc = nullptr;
            return false;
        }

        WebPAnimEncoderDelete(enc);
        enc = nullptr;

        encoded_data.assign(webp.bytes, webp.bytes + webp.size);
        WebPDataClear(&webp);

        if (!config.save_payload.empty())
            webp_set_save_string_as_xmp_inplace(encoded_data, config.save_payload);

        std::lock_guard<std::mutex> lock(capture_manager->encoded_data_mutex);
        capture_manager->encoded_data = std::move(encoded_data);
        capture_manager->capture_to_memory_complete.store(true, std::memory_order_release);
    }

    capture_manager->onFinalized(false);

    return true;
}

// ────── CaptureManager ──────

bool CaptureManager::startCapture(CaptureConfig _config)
{
    config = _config;
    frame_count = 0;
    capture_to_memory_complete.store(false, std::memory_order_release);

    #if BITLOOP_FFMPEG_ENABLED
    if (config.format == CaptureFormat::x264 || config.format == CaptureFormat::x265)
    {
        recording.store(true, std::memory_order_release);

        encoder_thread = std::jthread(&FFmpegWorker::process, &ffmpeg_worker, this, config);
    }
    else
    #endif

    if (config.format == CaptureFormat::WEBP_VIDEO)
    {
        recording.store(true, std::memory_order_release);

        encoder_thread = std::jthread(&WebPWorker::process, &webp_worker, this, config);
    }
    else if (config.format == CaptureFormat::WEBP_SNAPSHOT)
    {
        snapshotting.store(true, std::memory_order_release);

        encoder_thread = std::jthread(&WebPWorker::process, &webp_worker, this, config);
    }

    blPrint() << "startRecording()";

    return true;
}

void CaptureManager::finalizeCapture()
{
    // public facing:
    //   ask encoder thread to finalize after the next frame (or immediately if idle)
        
    finalize_requested.store(true, std::memory_order_release);
    work_available_cond.notify_one();
}

void CaptureManager::onFinalized(bool error)
{
    // called by encoder thread upon finalized...

    blPrint() << "onFinalized()";

    // No longer recording/snapshotting
    recording.store(false, std::memory_order_release);
    snapshotting.store(false, std::memory_order_release);

    // Encoder no longer busy
    encoder_busy.store(false, std::memory_order_release);

    any_capture_complete.store(true, std::memory_order_release);
    capture_error.store(error, std::memory_order_release);
    clearFinalizeRequest();

    frame_count = 0;

    // No more work to do - notify waiters.
    /// todo: Still needed if we only ever call from encoder thread?
    work_available_cond.notify_all();

    // Wake any sim thread
    encoder_ready_cond.notify_all();

    blPrint() << "finalizeCapture()";
}

bool CaptureManager::encodeFrame(const EncodeFrame& preprocessed_frame)
{
    // If encoder still busy with previous frame, do not block here
    if (encoder_busy.load(std::memory_order_acquire))
    {
        return false;
    }

    // Move the frame into 'pending_frame' for the encoder thread to grab
    {
        blPrint() << "Set pending_frame...";

        // snapshot encoder should only ever recieve ONE frame
        assert(!(isSnapshotting() && frame_count == 1));

        std::lock_guard<std::mutex> lock(pending_mutex);

        // copy all data to pending_frame (pixels, frame metadata, etc)
        pending_frame.loadFrom(preprocessed_frame);
        frame_count++;

        // Mark busy only after the buffer is fully populated
        blPrint() << "encoder_busy.store(true)";
        encoder_busy.store(true, std::memory_order_release);
    }

    work_available_cond.notify_one();
    return true;
}

void CaptureManager::waitUntilReadyForNewFrame()
{
    blPrint() << "waitUntilReadyForNewFrame()...";

    std::unique_lock<std::mutex> lock(encoder_ready_mutex);


    blPrint() << "encoder_ready_cond.wait";
    encoder_ready_cond.wait(lock, [this]
    {
        return !encoder_busy.load(std::memory_order_acquire) || !(isRecording() || isSnapshotting());
    });
}

bool CaptureManager::waitForWorkAvailable()
{
    blPrint() << "Waiting for work...";
    std::unique_lock<std::mutex> lock(work_available_mutex);
    work_available_cond.wait(lock, [this]{
        return encoder_busy.load(std::memory_order_acquire) || finalize_requested.load(std::memory_order_acquire);
    });

    return true;
}

EncodeFrame CaptureManager::takePendingFrame()
{
    blPrint() << "takePendingFrame()...";
    std::lock_guard<std::mutex> lock(pending_mutex);
    EncodeFrame out;
    out.swap(pending_frame);
    return out;
}

void CaptureManager::markEncoderIdle()
{
    blPrint() << "markEncoderIdle()";
    encoder_busy.store(false, std::memory_order_release);

    // Wake the sim worker (so it can proceed to produce the next frame)
    encoder_ready_cond.notify_all();
}

BL_END_NS;
