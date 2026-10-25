#pragma once
#include"Render/renderMath/RenderMath.h"

namespace X_Y {
	class Camera {
	public:
		Camera() = default;
		Camera(const RenderMath::Mat4& projection)
			: m_Projection(projection) {
		}

		virtual ~Camera() = default;

		const RenderMath::Mat4& GetProjection() const { return m_Projection; }
	protected:
		RenderMath::Mat4 m_Projection = RenderMath::Mat4(1.0f);
	};
}
