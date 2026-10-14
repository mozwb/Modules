#include "UI/Panel/MediaPlayerPanel.h"

#include "UI/Component/Button.h"
#include "UI/Component/Label.h"
#include "UI/Component/VideoView.h"
#include "XCore/Timer/Timer.h"
#include "Widget/FontLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace X_Y
{
	class MediaSeekBar final : public Component
	{
	public:
		void SetDuration(double seconds)
		{
			m_Duration = std::max(0.0, seconds);
			m_Dragging = false;
			m_Progress = 0.0;
			RequestRepaint();
		}

		void SetProgress(double progress)
		{
			if (m_Dragging)
				return;
			const double boundedProgress = std::clamp(progress, 0.0, 1.0);
			if (std::abs(boundedProgress - m_Progress) < 0.0001)
				return;
			m_Progress = boundedProgress;
			RequestRepaint();
		}

		void SetOnSeek(std::function<void(double)> callback)
		{
			m_OnSeek = std::move(callback);
		}

		void OnPaint(Canvas &canvas) override
		{
			const int width = GetWidth();
			const int height = GetHeight();
			canvas.FillRect(0, 0, width, height, 0xFF30343B);

			const int trackLeft = 8;
			const int trackRight = std::max(trackLeft + 1, width - 8);
			const int trackWidth = trackRight - trackLeft;
			const int trackY = height - 5;
			const int thumbX = trackLeft +
				static_cast<int>(std::lround(m_Progress * trackWidth));
			canvas.FillRect(trackLeft, trackY - 1, trackWidth, 3, 0xFF686D75);
			canvas.FillRect(
				trackLeft,
				trackY - 1,
				std::max(0, thumbX - trackLeft),
				3,
				0xFF4DA3FF);
			canvas.FillCircle(thumbX, trackY, 5.0f, 0.0f, 0xFFFFFFFF);

			const double currentSeconds = m_Progress * m_Duration;
			char timeText[48] = {};
			std::snprintf(
				timeText,
				sizeof(timeText),
				"%s / %s",
				FormatTime(currentSeconds).c_str(),
				FormatTime(m_Duration).c_str());
			auto &font = FontLibrary::Instance().GetDefault();
			canvas.DrawText(font, 8, 1, timeText, 0xFFFFFFFF);
		}

		void OnInput(UIInputEvent &event) override
		{
			auto *mouse = dynamic_cast<UIMouseEvent *>(&event);
			if (!mouse || mouse->button != Input::Mouse::ButtonLeft)
				return;

			if (mouse->action == MouseAction::Press && m_Duration > 0.0)
			{
				m_Dragging = true;
				UpdateProgressFromX(event.x);
				return;
			}
			if (mouse->action == MouseAction::Move && m_Dragging)
			{
				UpdateProgressFromX(event.x);
				return;
			}
			if (mouse->action == MouseAction::Release && m_Dragging)
			{
				UpdateProgressFromX(event.x);
				m_Dragging = false;
				if (m_OnSeek)
					m_OnSeek(m_Progress * m_Duration);
			}
		}

	private:
		static std::string FormatTime(double seconds)
		{
			const auto totalSeconds = static_cast<unsigned long long>(
				std::max(0.0, seconds));
			const auto hours = totalSeconds / 3600;
			const auto minutes = (totalSeconds / 60) % 60;
			const auto remainingSeconds = totalSeconds % 60;
			char buffer[24] = {};
			if (hours > 0)
				std::snprintf(
					buffer,
					sizeof(buffer),
					"%llu:%02llu:%02llu",
					hours,
					minutes,
					remainingSeconds);
			else
				std::snprintf(
					buffer,
					sizeof(buffer),
					"%02llu:%02llu",
					totalSeconds / 60,
					remainingSeconds);
			return buffer;
		}

		void UpdateProgressFromX(int x)
		{
			const int trackLeft = 8;
			const int trackWidth = std::max(1, GetWidth() - 16);
			m_Progress = std::clamp(
				static_cast<double>(x - trackLeft) / trackWidth,
				0.0,
				1.0);
			RequestRepaint();
		}

		double m_Duration = 0.0;
		double m_Progress = 0.0;
		bool m_Dragging = false;
		std::function<void(double)> m_OnSeek;
	};

	MediaPlayerPanel::MediaPlayerPanel()
		: m_VideoView(new VideoView()),
		  m_PlayPauseButton(new Button()),
		  m_StopButton(new Button()),
		  m_ProgressBar(new MediaSeekBar()),
		  m_Status(new Label())
	{
		m_PlayPauseButton->SetText("Play");
		m_PlayPauseButton->SetTextColor(0xFFFFFFFF);
		m_PlayPauseButton->SetBgColor(0xFF42464D);
		m_PlayPauseButton->SetHoverColor(0xFF565C64);
		m_PlayPauseButton->SetOnClick([this]
		{
			if (m_Player.IsPlaying())
			{
				m_Player.Pause();
				m_PlayPauseButton->SetText("Play");
			}
			else if (m_Player.Play())
			{
				m_PlayPauseButton->SetText("Pause");
			}
			else
			{
				m_Status->SetText(m_Player.GetErrorMessage());
			}
			RequestRepaint();
		});
		m_StopButton->SetText("Stop");
		m_StopButton->SetTextColor(0xFFFFFFFF);
		m_StopButton->SetBgColor(0xFF42464D);
		m_StopButton->SetHoverColor(0xFF565C64);
		m_StopButton->SetOnClick([this]
		{
			if (m_Player.IsOpen() && m_Player.Stop())
			{
				m_VideoView->ClearFrame();
				m_PlayPauseButton->SetText("Play");
				m_ProgressBar->SetProgress(0.0);
			}
			else if (!m_Player.GetErrorMessage().empty())
			{
				m_Status->SetText(m_Player.GetErrorMessage());
			}
			RequestRepaint();
		});

		m_Status->SetColor(0xFFFFFFFF);
		m_Status->SetBgColor(0xFF30343B);
		m_Status->SetText("Drop a video file here or open one from the test app.");
		m_ProgressBar->SetOnSeek([this](double timeSeconds)
		{
			if (!m_Player.Seek(timeSeconds))
				m_Status->SetText(m_Player.GetErrorMessage());
			m_PlayPauseButton->SetText(
				m_Player.IsPlaying() ? "Pause" : "Play");
			RequestRepaint();
		});

		AddComponent(m_VideoView);
		AddComponent(m_PlayPauseButton);
		AddComponent(m_StopButton);
		AddComponent(m_ProgressBar);
		AddComponent(m_Status);

		m_FrameTicker = std::make_unique<Ticker>();
		m_FrameTicker->Start(16, [this]
		{
			UpdateVideoFrame();
		});
	}

	MediaPlayerPanel::~MediaPlayerPanel()
	{
		if (m_FrameTicker)
		{
			m_FrameTicker->Stop();
			m_FrameTicker->Join();
		}
		m_Player.Close();
	}

	bool MediaPlayerPanel::OpenFile(const XPath &path)
	{
		return FileDropPanel::OpenFile(path);
	}

	void MediaPlayerPanel::OnLayout()
	{
		constexpr int controlsHeight = 34;
		constexpr int progressHeight = 24;
		constexpr int statusHeight = 24;
		const int videoHeight =
			m_H > controlsHeight + progressHeight + statusHeight
									? m_H - controlsHeight - progressHeight - statusHeight
									: 0;
		m_VideoView->SetRect(0, 0, m_W, videoHeight);
		m_PlayPauseButton->SetRect(0, videoHeight, 80, controlsHeight);
		m_StopButton->SetRect(80, videoHeight, 80, controlsHeight);
		m_ProgressBar->SetRect(
			0, videoHeight + controlsHeight, m_W, progressHeight);
		m_Status->SetRect(
			0,
			videoHeight + controlsHeight + progressHeight,
			m_W,
			m_H - videoHeight - controlsHeight - progressHeight);
	}

	void MediaPlayerPanel::OnPaint(Canvas &canvas)
	{
		canvas.FillRect(0, 0, m_W, m_H, 0xFF151515);
		FileDropPanel::OnPaint(canvas);
	}

	bool MediaPlayerPanel::OnOpenFiles(const std::vector<XPath> &files)
	{
		m_VideoView->ClearFrame();
		m_PlayPauseButton->SetText("Play");
		m_ProgressBar->SetDuration(0.0);
		for (const XPath &file : files)
		{
			if (!m_Player.Open(file.Path()))
				continue;

			const Decode::MediaInfo info = m_Player.GetMediaInfo();
			m_ProgressBar->SetDuration(info.DurationSeconds);
			if (!info.HasVideo)
			{
				m_Status->SetText("This media file has no video stream.");
			}
			else
			{
				m_Status->SetText(
					file.getName() + " | " +
					std::to_string(info.Video.Width) + " x " +
					std::to_string(info.Video.Height));
			}
			m_PlayPauseButton->SetText("Pause");
			if (!m_Player.Play())
			{
				m_PlayPauseButton->SetText("Play");
				m_Status->SetText(m_Player.GetErrorMessage());
				return false;
			}
			RequestRepaint();
			return true;
		}

		m_PlayPauseButton->SetText("Play");
		m_Status->SetText(
			m_Player.GetErrorMessage().empty()
				? "Could not open a playable media file."
				: m_Player.GetErrorMessage());
		return false;
	}

	void MediaPlayerPanel::UpdateVideoFrame()
	{
		Decode::MediaFrame frame;
		if (m_Player.TakeVideoFrame(frame))
			m_VideoView->SetFrame(std::move(frame));
		const Decode::MediaInfo info = m_Player.GetMediaInfo();
		if (info.DurationSeconds > 0.0)
			m_ProgressBar->SetProgress(
				m_Player.GetCurrentTimeSeconds() / info.DurationSeconds);
	}
}
