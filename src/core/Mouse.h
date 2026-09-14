#pragma once

#include <queue>
#include "stdafx.h"
#include "MouseEvent.h"

class Mouse
{
public:
  void OnLeftPress(int x, int y);
  void OnLeftRelease(int x, int y);
  void OnRightPress(int x, int y);
  void OnRightRelease(int x, int y);
  void OnMiddlePress(int x, int y);
  void OnMiddleRelease(int x, int y);
  void OnWheelDown(int x, int y);
  void OnWheelUp(int x, int y);
  void OnMouseMove(int x, int y);
  void OnMouseRawMove(int x, int y);

  bool IsLeftPressed() const;
  bool IsMiddlePressed() const;
  bool IsRightPressed() const;

  int GetPosX() const;
  int GetPosY() const;
  MousePoint GetPos() const;

  bool IsEventBufferEmpty() const;
  MouseEvent ReadEvent();

private:
  bool m_leftIsPressed   = false;
  bool m_middleIsPressed = false;
  bool m_rightIsPressed  = false;
  
  MousePoint m_position;
  std::queue<MouseEvent> m_eventBuffer;
};