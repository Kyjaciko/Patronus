#include "MouseEvent.h"

MouseEvent::MouseEvent()
  : m_eventType(EventType::INVALID)
{
  m_position = {};
}

MouseEvent::MouseEvent(const EventType eventType, const int x, const int y)
  : m_eventType(eventType)
{
  m_position = {
    .x = x,
    .y = y
  };
}

bool MouseEvent::IsValid() const
{
  return m_eventType != EventType::INVALID;
}

MouseEvent::EventType MouseEvent::GetEventType() const
{
  return m_eventType;
}

MousePoint MouseEvent::GetPosition() const
{
  return m_position;
}

int MouseEvent::GetPosX() const
{
  return m_position.x;
}

int MouseEvent::GetPosY() const
{
  return m_position.y;
}