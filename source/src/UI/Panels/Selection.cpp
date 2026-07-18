#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/operations.h"
#include "../../ISO/IsoMount.h"
#include "../../BNKCore.cpp"
#include "../../GDB/GdbParser.h"
#include "../UI_Main.h"
#include "../AudioPlayerWindow.h"
#include "../OutputLog.h"
#include "ImportDialog.h"
#include "../../textures/export/TextureExport.h"
#include "../../Utilities/GameBackup.h"
#include "../../Audio/XmaDecoder.h"

#include "../../Audio/MfAudioEncoder.h"
#include "../../ISO/IsoDump.h"

#include "../../animations/AnimBank.h"
#include "../../animations/AnimDataFile.h"
#include "../../animations/Locomotion.h"
#include "imgui.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstring>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "Selection/Audio/Player.inl"

namespace {

#include "Selection/Models/Helpers.inl"

}

#include "Selection/Gdb/Cache.inl"
#include "Selection/Banks/Pick.inl"
#include "Selection/Sources/Iso.inl"
#include "Selection/Sources/Folder.inl"
#include "Selection/Models/ReconstructNested.inl"
#include "Selection/Audio/Paths.inl"
#include "Selection/Banks/Extract.inl"

namespace {

#include "Selection/Models/ReconstructPaired.inl"
#include "Selection/Export/Helpers.inl"

}

#include "Selection/Export/RawAudio.inl"
#include "Selection/Export/EncodedAudio.inl"
#include "Selection/Export/Asset.inl"
#include "Selection/Context/FileMenu.inl"
