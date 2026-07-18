#include "XmaWmaProDecoder.h"
#include "XmaBitstream.h"
#include "XmaSupport.h"
#include "XmaTx.h"
#include "XmaWmaCore.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace Xma {

namespace {

#include "wmaprodata.h"
#include "XmaWmaProDecoder/State/TablesAndState.inl"
#include "XmaWmaProDecoder/Math/Vector.inl"
#include "XmaWmaProDecoder/Decode/Initialize.inl"
#include "XmaWmaProDecoder/Decode/TilesAndTransforms.inl"
#include "XmaWmaProDecoder/Decode/Coefficients.inl"
#include "XmaWmaProDecoder/Decode/TransformsAndWindow.inl"
#include "XmaWmaProDecoder/Decode/Subframe.inl"
#include "XmaWmaProDecoder/Decode/Frame.inl"
#include "XmaWmaProDecoder/Packet/Bits.inl"
#include "XmaWmaProDecoder/Packet/Decode.inl"

}

#include "XmaWmaProDecoder/Decoder/Impl.inl"
#include "XmaWmaProDecoder/Decoder/Public.inl"

}
