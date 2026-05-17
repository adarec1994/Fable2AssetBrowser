#pragma once

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace About {

void open();

#ifdef _WIN32
void draw(ID3D11Device* device);
#else
void draw();
#endif

void release_resources();

}
