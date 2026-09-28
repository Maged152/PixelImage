#pragma once

#include "image.hpp"
#include "video_common.hpp"
#include <string>
#include <memory>

namespace qlm
{
	// Reads a video file one frame at a time.
	//
	// The MP4 container is demuxed by minimp4. Motion-JPEG tracks are decoded by
	// stb_image, while H.264 (AVC) tracks are decoded by edge264. See doc/video.md
	// for the details.
	//
	// A VideoReader owns the demuxer and the mapped file data, so it is movable but
	// not copyable.
	class VideoReader
	{
	private:
		struct Impl;
		std::unique_ptr<Impl> impl;

		// Fetches the sample, decodes it and stores the pixels in Impl.
		bool DecodeFrame(int frame_index);

	public:
		VideoReader();
		~VideoReader();

		VideoReader(VideoReader&& other) noexcept;
		VideoReader& operator=(VideoReader&& other) noexcept;

		VideoReader(const VideoReader&) = delete;
		VideoReader& operator=(const VideoReader&) = delete;

		bool LoadFromFile(const std::string& file_name);
		void Close();
		bool IsOpen() const;

		// Video encoding format (VideoFormat::MP4_MJPEG or VideoFormat::MP4_H264).
		VideoFormat Format() const;

		// Dimensions of the video track, taken from the first frame.
		int Width() const;
		int Height() const;

		int FrameCount() const;

		// Frames per second, 0 when the container does not state a frame duration.
		double FrameRate() const;

		// Total length of the track in seconds.
		double Duration() const;

		// Presentation time of the frame read last, in seconds.
		double Time() const;

		int FrameIndex() const;

		// Sequential access: decodes the frame at FrameIndex and advances the cursor.
		// Returns false at the end of the track or when the frame cannot be decoded.
		bool ReadFrame(Image<ImageFormat::RGB, uint8_t>& frame);
		bool ReadFrame(Image<ImageFormat::GRAY, uint8_t>& frame);

		// Random access: decodes frame_index, leaving the sequential cursor untouched.
		bool ReadFrame(int frame_index, Image<ImageFormat::RGB, uint8_t>& frame);
		bool ReadFrame(int frame_index, Image<ImageFormat::GRAY, uint8_t>& frame);

		bool Seek(int frame_index);
		bool Rewind();
		bool HasEnded() const;
	};
}
