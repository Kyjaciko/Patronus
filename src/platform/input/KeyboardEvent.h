#pragma once

#include <stdint.h>

class KeyboardEvent
{
public:
  enum class EventType : uint8_t
  {
    KEY_DOWN,
    KEY_UP,
    INVALID
  };

public:
  KeyboardEvent();
  KeyboardEvent(const EventType type, const unsigned char key);

  bool IsPressed() const;
  bool IsReleased() const;
  bool IsValid() const;
  unsigned char GetKey() const;

private:
  EventType m_type;
  unsigned char m_key;
};