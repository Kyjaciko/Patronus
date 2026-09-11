#pragma once

#include "stdafx.h"
#include "GameObject3D.h"

class Camera3D : public GameObject3D
{
public:
	Camera3D();
	Camera3D(float fovDegrees, float aspectRatio, float nearZ, float farZ);

	void SetProjectionValues(float fovDegrees, float aspectRatio, float nearZ, float farZ);

	const DirectX::XMMATRIX& GetViewMatrix() const;
	const DirectX::XMMATRIX& GetProjectionMatrix() const;

private:
	DirectX::XMMATRIX	m_ViewMatrix;
	DirectX::XMMATRIX	m_ProjectionMatrix;

private:
	virtual void UpdateMatrix() override;
};