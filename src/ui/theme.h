#pragma once
// BLUEWARD's palette, sampled from the Promilia key visual: navy ground, pink primary accent, cream
// text, gold secondary. Values follow launcher-assets/MEDIA.md В§6.

#include <d2d1.h>

namespace blueward::theme {

inline const D2D1_COLOR_F kBg = D2D1::ColorF(0x0B0A24);
inline const D2D1_COLOR_F kPanel = D2D1::ColorF(0x171533);
inline const D2D1_COLOR_F kPanelRaised = D2D1::ColorF(0x221F45);
inline const D2D1_COLOR_F kAccent = D2D1::ColorF(0xE58FBE);
inline const D2D1_COLOR_F kGold = D2D1::ColorF(0xE8C597);
inline const D2D1_COLOR_F kText = D2D1::ColorF(0xF7F1F8);
inline const D2D1_COLOR_F kMuted = D2D1::ColorF(0xA79BC9);
inline const D2D1_COLOR_F kButton = D2D1::ColorF(0xA93E76);
inline const D2D1_COLOR_F kButtonHover = D2D1::ColorF(0xB0447C);
inline const D2D1_COLOR_F kDanger = D2D1::ColorF(0xC23B54);
inline const D2D1_COLOR_F kLink = D2D1::ColorF(0x8E8FE0);
inline const D2D1_COLOR_F kOk = D2D1::ColorF(0x8FE3B0);

inline constexpr float kHeaderHeight = 56.0f;
inline constexpr float kMinWidth = 760.0f;
inline constexpr float kMinHeight = 520.0f;
inline constexpr float kDefaultWidth = 900.0f;
inline constexpr float kDefaultHeight = 620.0f;
inline constexpr float kCardWidth = 480.0f;

inline D2D1_COLOR_F Alpha(D2D1_COLOR_F color, float alpha) { return D2D1::ColorF(color.r, color.g, color.b, alpha); }

}  // namespace blueward::theme

