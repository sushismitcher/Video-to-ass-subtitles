#include <fstream>
#include <iomanip> // For std::setfill and std::setw
#include <iostream>
#include <sstream> // For std::stringstream
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h> // For AV_TIME_BASE (though not directly used here, good to have for time)
#include <libswscale/swscale.h>
}

struct RGB {
  uint8_t r, g, b;

  std::string to_ass() const {
    char buf[16];
    // ASS uses BBGGRR format, so swap R and B
    snprintf(buf, sizeof(buf), "&H%02X%02X%02X&", b, g, r);
    return std::string(buf);
  }
};

// Moved Frame class definition before VideoReader
class Frame {
public:
  std::vector<RGB> pixels;
  int width, height;

  RGB get_pixel(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) {
      return {0, 0, 0};
    }
    return pixels[y * width + x];
  }
};

class VideoReader {
private:
  AVFormatContext *fmt_ctx = nullptr;
  AVCodecContext *codec_ctx = nullptr;
  SwsContext *sws_ctx = nullptr;
  AVFrame *av_frame = nullptr;
  AVFrame *rgb_frame = nullptr;
  AVPacket *packet = nullptr;
  uint8_t *rgb_buffer = nullptr;

  int video_stream_idx = -1;
  int current_frame_num = 0;

  bool decode_next_frame(Frame &frame) { // Frame is now defined
    while (av_read_frame(fmt_ctx, packet) >= 0) {
      if (packet->stream_index == video_stream_idx) {
        if (avcodec_send_packet(codec_ctx, packet) >= 0) {
          if (avcodec_receive_frame(codec_ctx, av_frame) >= 0) {
            // convert to rgb24
            sws_scale(sws_ctx, av_frame->data, av_frame->linesize, 0,
                      frame.height, rgb_frame->data, rgb_frame->linesize);

            // copy to vector
            for (int y = 0; y < frame.height; y++) {
              uint8_t *row = rgb_frame->data[0] + y * rgb_frame->linesize[0];
              for (int x = 0; x < frame.width; x++) {
                int idx = y * frame.width + x;
                frame.pixels[idx].r = row[x * 3 + 0];
                frame.pixels[idx].g = row[x * 3 + 1];
                frame.pixels[idx].b = row[x * 3 + 2];
              }
            }

            av_packet_unref(packet);
            current_frame_num++;
            return true;
          }
        }
      }
      av_packet_unref(packet);
    }
    return false;
  }

  void seek_to_start() {
    av_seek_frame(fmt_ctx, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec_ctx);
    current_frame_num = 0;
  }

public:
  int width = 0;
  int height = 0;

  VideoReader(const char *filename) {
    if (avformat_open_input(&fmt_ctx, filename, nullptr, nullptr) < 0) {
      std::cerr << "couldn't open file\n";
      return;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
      std::cerr << "couldn't find stream info\n";
      return;
    }

    // find video stream
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
      if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_stream_idx = i;
        break;
      }
    }

    if (video_stream_idx == -1) {
      std::cerr << "no video stream found\n";
      return;
    }

    AVStream *video_stream = fmt_ctx->streams[video_stream_idx];
    const AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);
    avcodec_open2(codec_ctx, codec, nullptr);

    width = codec_ctx->width;
    height = codec_ctx->height;

    av_frame = av_frame_alloc();
    rgb_frame = av_frame_alloc();

    // allocate rgb buffer
    int rgb_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
    rgb_buffer = (uint8_t *)av_malloc(rgb_size);
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer,
                         AV_PIX_FMT_RGB24, width, height, 1);

    // setup scaler
    sws_ctx = sws_getContext(
        width, height, codec_ctx->pix_fmt,
        width, height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    packet = av_packet_alloc();
  }

  ~VideoReader() {
    if (packet)
      av_packet_free(&packet);
    if (rgb_buffer)
      av_free(rgb_buffer);
    if (rgb_frame)
      av_frame_free(&rgb_frame);
    if (av_frame)
      av_frame_free(&av_frame);
    if (sws_ctx)
      sws_freeContext(sws_ctx);
    if (codec_ctx)
      avcodec_free_context(&codec_ctx);
    if (fmt_ctx)
      avformat_close_input(&fmt_ctx);
  }

  bool is_open() const {
    return fmt_ctx != nullptr && codec_ctx != nullptr;
  }

  Frame get_frame(int frame_num) { // Frame is now defined
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.pixels.resize(width * height);

    if (!is_open()) {
      std::cerr << "video not open\n";
      return frame;
    }

    // if seeking backwards, go back to start
    if (frame_num < current_frame_num) {
      seek_to_start();
    }

    // decode until we reach target frame
    while (current_frame_num <= frame_num) {
      if (!decode_next_frame(frame)) {
        // If we can't decode, it might be end of stream.
        // Reset pixels to be empty to signal failure/end.
        frame.pixels.clear();
        std::cerr << "couldn't decode frame " << frame_num << " or end of stream reached.\n";
        return frame;
      }
    }

    return frame;
  }

  int get_current_frame_num() const { // Renamed to avoid confusion with `get_frame` return
    return current_frame_num - 1;
  }

  // Public getter for the video stream to access frame rate
  AVStream *get_video_stream() const {
    if (fmt_ctx && video_stream_idx != -1) {
      return fmt_ctx->streams[video_stream_idx];
    }
    return nullptr;
  }
};

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <video_file>\n";
    return 1;
  }

  VideoReader reader(argv[1]);

  if (!reader.is_open()) {
    std::cerr << "failed to open video\n";
    return 1;
  }

  std::cout << "video size: " << reader.width << "x" << reader.height << "\n";

  // --- Editable variables ---
  int pixel_block_size = 40;       // Size of each "pixel" block (e.g., 10x10 pixels)
  int frame_process_interval = 20; // Process every Nth frame (e.g., every 10th frame)
  // --------------------------

  std::ofstream file("output.ass");

  file << "[Script Info]\n";
  file << "Title: Pixelated Video Subtitles\n";
  file << "ScriptType: v4.00+\n";
  file << "PlayResX: " << reader.width << "\n";    // Use actual video width
  file << "PlayResY: " << reader.height << "\n\n"; // Use actual video height
  file << "Timer: 100.0000\n\n";

  file << "[V4+ Styles]\n";
  file << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n";
  file << "Style: Pixel,Arial," << pixel_block_size << ",&H00FFFFFF,&H00000000,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1\n\n";

  file << "[Events]\n";
  file << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

  int current_output_frame = 0;
  Frame frame;

  // We need to calculate timecodes for ASS subtitles
  // To do this, we need the frame rate from the video stream
  AVStream *video_stream = reader.get_video_stream(); // Use the public getter
  double fps = 0.0;
  if (video_stream) {
    fps = av_q2d(video_stream->avg_frame_rate);
  }

  if (fps <= 0) {
    std::cerr << "Could not determine frame rate. Using a default of 25 FPS.\n";
    fps = 25.0; // Default if not found
  }

  std::cout << "Video FPS: " << fps << "\n";

  while (true) {
    // Get the next frame. The VideoReader's get_frame method handles seeking.
    frame = reader.get_frame(current_output_frame);

    if (frame.pixels.empty()) {
      break; // No more frames or failed to decode
    }

    if (current_output_frame % frame_process_interval == 0) {
      std::cout << "Processing frame " << current_output_frame << "...\n";

      // Calculate start and end time for the current block of subtitles
      // ASS time format: H:MM:SS.CC (centiseconds)
      double start_time_seconds = current_output_frame / fps;
      // The end time should be the start of the next interval, or the actual end of video
      double end_time_seconds = (current_output_frame + frame_process_interval) / fps;

      auto format_time = [](double seconds) {
        int h = static_cast<int>(seconds / 3600);
        seconds -= h * 3600;
        int m = static_cast<int>(seconds / 60);
        seconds -= m * 60;
        int s = static_cast<int>(seconds);
        int cs = static_cast<int>((seconds - s) * 100);
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(1) << h << ":"
           << std::setfill('0') << std::setw(2) << m << ":"
           << std::setfill('0') << std::setw(2) << s << "."
           << std::setfill('0') << std::setw(2) << cs;
        return ss.str();
      };

      std::string start_time_str = format_time(start_time_seconds);
      std::string end_time_str = format_time(end_time_seconds);

      // Iterate through the frame to create pixel blocks
      for (int y = 0; y < frame.height; y += pixel_block_size) {
        for (int x = 0; x < frame.width; x += pixel_block_size - 25) { // -12 is good for pixel block size = 20.
          RGB p = frame.get_pixel(x, y);

          file << "Dialogue: 0," << start_time_str << "," << end_time_str
               << ",Pixel,,0,0,0,,{\\pos("
               << x << "," << y << ")\\1c" << p.to_ass() << "}█\n";
        }
      }
    }
    current_output_frame++;
  }

  file.close();
  std::cout << "\nSuccessfully generated output.ass\n";
  return 0;
}
