#pragma once
//
// Main 3-column layout (post-loading):
//
//   ┌──────────┬─────────────────────┬─────────────────────────────┐
//   │          │  [Tree]  [Banks]    │                             │
//   │ sidebar  │  ─────────────────  │      content / preview      │
//   │ (logo +  │  file tree view     │      (file table + actions) │
//   │ branding)│  (or BNK list)      │                             │
//   │          │                     │                             │
//   └──────────┴─────────────────────┴─────────────────────────────┘
//
// The sidebar is a fixed 180 px column on the left. The middle column
// holds the tabbed [File Tree | BNK List] panel and is sized as a
// fixed proportion of the remaining width. The content panel takes
// whatever's left.
//

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace UI {

#ifdef _WIN32
void draw_main_layout(ID3D11Device* device);
#else
void draw_main_layout();
#endif

} // namespace UI
