#include "Mouse.h"

void Mouse::OnLeftPress(int x, int y)
{
  m_leftIsPressed = true;
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::LEFT_DOWN, x, y));
}

void Mouse::OnLeftRelease(int x, int y)
{
  m_leftIsPressed = false;
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::LEFT_UP, x, y));
}

void Mouse::OnRightPress(int x, int y)
{
  m_rightIsPressed = true;
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::RIGHT_DOWN, x, y));
}

void Mouse::OnRightRelease(int x, int y)
{
  m_rightIsPressed = false;
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::RIGHT_UP, x, y));
}

void Mouse::OnMiddlePress(int x, int y)
{
  m_middleIsPressed = true;
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::MIDDLE_DOWN, x, y));
}

void Mouse::OnMiddleRelease(int x, int y)
{
  m_middleIsPressed = false;
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::MIDDLE_UP, x, y));
}

void Mouse::OnWheelDown(int x, int y)
{
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::WHEEL_DOWN, x, y));
}

void Mouse::OnWheelUp(int x, int y)
{
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::WHEEL_UP, x, y));
}

void Mouse::OnMouseMove(int x, int y)
{
  // Mouse move is the absolute position.
  m_position.x = x;
  m_position.y = y;
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::MOVE, x, y));
}

void Mouse::OnMouseRawMove(int x, int y)
{
  // RAW mouse move is relative to the last position.
  m_eventBuffer.push(MouseEvent(MouseEvent::EventType::RAW_MOVE, x, y));
}

bool Mouse::IsLeftPressed() const
{
  return m_leftIsPressed;
}

bool Mouse::IsMiddlePressed() const
{
  return m_middleIsPressed;
}

bool Mouse::IsRightPressed() const
{
  return m_rightIsPressed;
}

int Mouse::GetPosX() const
{
  return m_position.x;
}

int Mouse::GetPosY() const
{
  return m_position.y;
}

MousePoint Mouse::GetPos() const
{
  return m_position;
}

bool Mouse::IsEventBufferEmpty() const
{
  return m_eventBuffer.empty();
}

MouseEvent Mouse::ReadEvent()
{
  if (m_eventBuffer.empty()) return MouseEvent();
  
  MouseEvent event = m_eventBuffer.front();
  m_eventBuffer.pop();
  return event;
}