#pragma once

#include "XCore/Decoder/Blueprint/Media.h"

#include <filesystem>
#include <memory>
#include <string>

namespace X_Y::Media
{
	enum class PlaybackState
	{
		Closed,
		Ready,
		Playing,
		Paused,
		Ended,
		Error
	};

	class MediaPlayer
	{
	public:
		MediaPlayer();
		~MediaPlayer();

		MediaPlayer(const MediaPlayer &) = delete;
		MediaPlayer &operator=(const MediaPlayer &) = delete;

		bool Open(const std::filesystem::path &filePath);
		bool Play();
		void Pause();
		bool Stop();
		bool Seek(double timeSeconds);
		void Close();

		bool TakeVideoFrame(Decode::MediaFrame &frame);
		bool IsOpen() const;
		bool IsPlaying() const;
		PlaybackState GetState() const;
		double GetCurrentTimeSeconds() const;
		Decode::MediaInfo GetMediaInfo() const;
		std::string GetErrorMessage() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
