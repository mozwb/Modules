#pragma once

#include "UI/Panel/FileDropPanel.h"
#include "Media/MediaPlayer.h"

#include <memory>

namespace X_Y
{
	class Button;
	class Label;
	class MediaSeekBar;
	class Ticker;
	class VideoView;

	class MediaPlayerPanel final : public FileDropPanel
	{
	public:
		MediaPlayerPanel();
		~MediaPlayerPanel() override;

		bool OpenFile(const XPath &path);

	protected:
		void OnLayout() override;
		void OnPaint(Canvas &canvas) override;
		bool OnOpenFiles(const std::vector<XPath> &files) override;

	private:
		void UpdateVideoFrame();

		Media::MediaPlayer m_Player;
		VideoView *m_VideoView = nullptr;
		Button *m_PlayPauseButton = nullptr;
		Button *m_StopButton = nullptr;
		MediaSeekBar *m_ProgressBar = nullptr;
		Label *m_Status = nullptr;
		std::unique_ptr<Ticker> m_FrameTicker;
	};
}
