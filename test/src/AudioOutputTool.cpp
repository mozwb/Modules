#include "Media/MediaPlayer.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace
{
	// Set this to an audio file on your machine, or pass its path on the command line.
	const std::filesystem::path audioFilePath =
		R"(D:\path\to\your\audio-file.mp3)";

	int RunSelfTest()
	{
		X_Y::Media::MediaPlayer player;
		if (player.Open("xy_media_player_missing_fixture.wav") || player.IsOpen() ||
			player.GetErrorMessage().empty())
		{
			std::cerr << "MediaPlayer did not report an invalid input file\n";
			return 1;
		}
		if (player.Play() || player.Stop())
		{
			std::cerr << "MediaPlayer accepted controls without an open file\n";
			return 1;
		}
		player.Close();
		if (player.IsOpen())
		{
			std::cerr << "MediaPlayer remained open after Close\n";
			return 1;
		}
		return 0;
	}

	int PlayAudio(const std::filesystem::path &filePath)
	{
		std::error_code fileError;
		if (!std::filesystem::is_regular_file(filePath, fileError))
		{
			std::cerr << "Audio file is not accessible: " << filePath.string();
			if (fileError)
				std::cerr << " (" << fileError.message() << ')';
			std::cerr << '\n';
			return 1;
		}

		X_Y::Media::MediaPlayer player;
		if (!player.Open(filePath))
		{
			std::cerr << "Failed to open audio: " << player.GetErrorMessage() << '\n';
			return 1;
		}
		if (!player.Play())
		{
			std::cerr << "Failed to start audio: " << player.GetErrorMessage() << '\n';
			return 1;
		}

		std::cout << "Playing: " << filePath.string() << '\n'
				  << "Playback will exit when the audio finishes.\n";
		while (player.IsPlaying())
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

		const std::string error = player.GetErrorMessage();
		if (!error.empty())
		{
			std::cerr << "Audio playback reported an error: " << error << '\n';
			return 1;
		}
		std::cout << "Playback finished.\n";
		return 0;
	}

	int CheckVideoPlayback(const std::filesystem::path &filePath)
	{
		X_Y::Media::MediaPlayer player;
		if (!player.Open(filePath))
		{
			std::cerr << "Failed to open media: " << player.GetErrorMessage() << '\n';
			return 1;
		}

		const X_Y::Decode::MediaInfo info = player.GetMediaInfo();
		if (!info.HasVideo)
		{
			std::cerr << "The media file has no video stream\n";
			return 1;
		}
		if (!player.Play())
		{
			std::cerr << "Failed to start media: " << player.GetErrorMessage() << '\n';
			return 1;
		}

		uint64_t frameCount = 0;
		const auto deadline = std::chrono::steady_clock::now() +
							  std::chrono::seconds(5);
		while (player.IsPlaying() &&
			   std::chrono::steady_clock::now() < deadline)
		{
			X_Y::Decode::MediaFrame frame;
			if (player.TakeVideoFrame(frame))
			{
				if (!frame.Data.Data || frame.Width == 0 || frame.Height == 0)
				{
					std::cerr << "Received an invalid video frame\n";
					return 1;
				}
				++frameCount;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(4));
		}

		player.Close();
		if (frameCount == 0)
		{
			std::cerr << "No video frames were presented\n";
			return 1;
		}
		std::cout << "Presented " << frameCount
				  << " video frames (" << info.Video.Width << " x "
				  << info.Video.Height << ").\n";
		return 0;
	}
}

int main(int argc, char *argv[])
{
	if (argc == 2 && std::string_view(argv[1]) == "--self-test")
		return RunSelfTest();
	if (argc == 3 && std::string_view(argv[1]) == "--video-check")
		return CheckVideoPlayback(argv[2]);

	if (argc > 2)
	{
		std::cerr << "Usage: XYAudioOutputTool [audio-file-path | --self-test | --video-check audio-file-path]\n";
		return 2;
	}

	const std::filesystem::path filePath =
		argc == 2 ? std::filesystem::path(argv[1]) : audioFilePath;
	return PlayAudio(filePath);
}
