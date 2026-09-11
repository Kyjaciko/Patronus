#pragma once

#include <cmath>
#include "stdafx.h"
#include "GameObject.h"

class GameObject3D : public GameObject
{
public:
	void SetLookAtPosition(DirectX::XMFLOAT3 lookAtPosition);

	const DirectX::XMVECTOR& GetForwardVector(bool omitY = false) const;
	const DirectX::XMVECTOR& GetLeftVector(bool omitY = false) const;
	const DirectX::XMVECTOR& GetRightVector(bool omitY = false) const;
	const DirectX::XMVECTOR& GetBackwardVector(bool omitY = false) const;
	const DirectX::XMVECTOR& GetUpVector() const;

protected:
	// Position.
	DirectX::XMVECTOR	m_ForwardVector;
	DirectX::XMVECTOR	m_LeftVector;
	DirectX::XMVECTOR	m_RightVector;
	DirectX::XMVECTOR	m_BackwardVector;
	DirectX::XMVECTOR m_UpVector;

	DirectX::XMVECTOR	m_ForwardVector_NO_Y;
	DirectX::XMVECTOR	m_LeftVector_NO_Y;
	DirectX::XMVECTOR	m_RightVector_NO_Y;
	DirectX::XMVECTOR	m_BackwardVector_NO_Y;

  // Right-handed (RH) coordinate system.
	const DirectX::XMVECTOR DEFAULT_FORWARD_VECTOR  = DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f); // Negative Z axis.
	const DirectX::XMVECTOR DEFAULT_UP_VECTOR       = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);  // Positive Y axis.
	const DirectX::XMVECTOR DEFAULT_BACKWARD_VECTOR = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);  // Positive Z axis.
	const DirectX::XMVECTOR DEFAULT_LEFT_VECTOR     = DirectX::XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f); // Negative X axis.
	const DirectX::XMVECTOR DEFAULT_RIGHT_VECTOR    = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);  // Positive X axis.

protected:
	void UpdateDirectionVectors();
};