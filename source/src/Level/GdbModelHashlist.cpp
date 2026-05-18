#include "GdbModelHashlist.h"

#include <cctype>

namespace Level::GdbModelHashlist {
namespace {

struct HashOverride {
    uint32_t hash;
    const char* model_path;
};

struct KeyOverride {
    const char* key;
    const char* model_path;
};

// Parent-hash archetypes mined from the all-level GDB/model corpus. Keep this
// table conservative: only stable parent hashes where the same model won every
// sampled placement are included.
constexpr HashOverride kParentHashOverrides[] = {
    { 0xA494A07E, "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_TownHouse_Basic_Facade_Mid\\BS_TownHouse_Basic_Facade_Mid.mdl" }, // 429x NewObjectBuildingBSMarketTownhouseSmall
    { 0x957AC8B3, "Art\\Environment\\Shared assets\\Doors_Windows\\dotXSI\\ESA_MdBoxWin\\ESA_MdBoxWin\\ESA_MdBoxWin.mdl" }, // 182x ESA_MdBoxWin
    { 0xC689EAF2, "Art\\Environment\\Shared assets\\Walls\\dotXSI\\Stone_Wall_Medium_Straight_Spiked\\Stone_Wall_Medium_Straight_Spiked.mdl" }, // 116x ObjectFurnitureSmallWallStraight
    { 0x26EAF370, "Art\\Environment\\Shared assets\\Walls\\dotXSI\\Stone_Wall_Medium_Post_Spiked\\Stone_Wall_Medium_Post_Spiked.mdl" }, // 112x NewObjectFurnitureSmallWallPost
    { 0x5CAC0AD8, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Light_Ceiling\\BS_Light_Ceiling.mdl" }, // 110x LightFixing_Ceiling
    { 0xF65A490C, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Books_Block_V2\\ESA_Books_Block_V2.mdl" }, // 92x Books_Group_V3
    { 0x00D4DC18, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_CandleHolder\\BS_CandleHolder.mdl" }, // 88x Candleholder
    { 0x88E93139, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shelf_Long\\ESA_Shelf_Long.mdl" }, // 85x Shelf_Long
    { 0x8484ACFB, "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Tower_Railings\\BW_Tower_Railings.mdl" }, // 76x NewObjectFurnitureStaticBWTowerRailings
    { 0xCAAC984A, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Cemetary_OilLamp_Single\\BS_Cemetary_OilLamp_Single.mdl" }, // 76x Oil_Lantern_Single
    { 0x6DA03555, "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_TownHouse_V3\\BS_TownHouse_V3\\exterior.mdl" }, // 71x ObjectBuildingBSMarketTownhouse_V3
    { 0xA1B73733, "Art\\Environment\\Shared assets\\Doors_Windows\\dotXSI\\ESA_TISmBoxWin\\ESA_TISmBoxWin\\ESA_TISmBoxWin.mdl" }, // 68x ESA_TISmBoxWin
    { 0x5F76D58D, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Sack_Grain\\ESA_Sack_Grain.mdl" }, // 54x GrainSack
    { 0x21710548, "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Tower_ComfyChair\\BW_Tower_ComfyChair.mdl" }, // 52x ComfyChair
    { 0xD09C4046, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Books_Block_V2\\ESA_Books_Block_V2.mdl" }, // 52x Books_Group_V6
    { 0x5809CE66, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shipping_Crate_V1\\ESA_Shipping_Crate_V1.mdl" }, // 50x ShippingCrate33
    { 0x2AC5FE9D, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Books_Block_V2\\ESA_Books_Block_V2.mdl" }, // 46x Books_Group_V1
    { 0x391FC1AF, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Docks_Arch\\BS_Market_Docks_Arch.mdl" }, // 46x NewObjectBuildingBSMarketDockArch
    { 0x3C712E28, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shipping_Crate_V1\\ESA_Shipping_Crate_V1.mdl" }, // 46x ShippingCrate2
    { 0xAAA7BC16, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Buffer\\BS_Market_Wall_Buffer.mdl" }, // 44x NewObjectBuildingBSMarketWallJoiner
    { 0x69FBCCA0, "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Tower_Book1\\BW_Tower_Book1.mdl" }, // 40x ObjectFurnitureStaticBWTowerBook14
    { 0x1734CEB5, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Quill\\ESA_Quill.mdl" }, // 36x Quill
    { 0xC88FFFC5, "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Tower_Railings\\BW_Tower_Railings.mdl" }, // 36x NewObjectFurnitureBWTowerRailings10Slope
    { 0xB1E8F042, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shipping_Crate_V1\\ESA_Shipping_Crate_V1.mdl" }, // 34x ShippingCrate7
    { 0xE1B71A67, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Docks_Wall\\BS_Market_Docks_Wall.mdl" }, // 34x BS_Dock_Wall
    { 0xA60B3E54, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Table_Tavern\\ESA_Table_Tavern.mdl" }, // 32x NewPubTable
    { 0x182FC5DA, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_MannequinTorso\\BS_MannequinTorso.mdl" }, // 30x NewMannequinTorso
    { 0x3A94749C, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Dresser_UltraDecorative\\ESA_F_Dresser_UltraDecorative.mdl" }, // 30x Dresser_Upgradeable
    { 0x452FF528, "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_TownHouse_V1_Facade_Mid\\BS_TownHouse_V1_Facade_Mid.mdl" }, // 30x NewObjectBuildingBSMarketTownhouse_V1
    { 0x57B7AC80, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_HangingBasket\\ESA_HangingBasket.mdl" }, // 30x NewHangingBasket
    { 0x60401D1B, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_GrandfatherClock\\BS_GrandfatherClock.mdl" }, // 30x NewGrandfatherClock
    { 0xE7A69170, "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Large_Room\\Cellar_Large_Room.mdl" }, // 27x Cellar_Large_Room
    { 0x9D3AC8F3, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Chair_Wooden_UltraDecorative\\ESA_F_Chair_Wooden_UltraDecorative.mdl" }, // 26x Chair_Wooden_UltraDecorative
    { 0xC1DAB8D5, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Till\\BS_Till.mdl" }, // 24x NewTill
    { 0x021C9574, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Slums_ThinWall_V1\\BS_Slums_ThinWall_V1.mdl" }, // 22x BS_Slums_Wall
    { 0x19EFE345, "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_TownHouse_V2_Facade_Mid\\BS_TownHouse_V2_Facade_Mid.mdl" }, // 22x NewFacadeTownhouseV2
    { 0x45379F1C, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Bookcase_Normal\\ESA_F_Bookcase_Normal.mdl" }, // 22x Bookcase_Normal
    { 0xA59C6E51, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_HayBale_Block\\ESA_HayBale_Block.mdl" }, // 22x HayBale_Block
    { 0xA7715468, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Straight\\BS_Market_Wall_Straight.mdl" }, // 22x NewObjectBuildingBSMarketWallStraight2
    { 0xB7BCCB70, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shipping_Crate_V1\\ESA_Shipping_Crate_V1.mdl" }, // 22x ShippingCrate3Lid
    { 0x8828D71A, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Fireplace_Grate\\ESA_Fireplace_Grate.mdl" }, // 20x NewObjectFurnitureFireplaceGrate
    { 0xCBBFEF0D, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_BuntingVertical\\BS_BuntingVertical.mdl" }, // 20x NewObjectFurnitureBuntingVertical
    { 0xC6CBF78E, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Table_Decorative\\ESA_F_Table_Decorative.mdl" }, // 17x Table_LargeSquare_Upgradeable
    { 0x0B22B56D, "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Corridor_Stairs\\Cellar_Corridor_Stairs.mdl" }, // 16x Cellar_Corridor_Stairs
    { 0x303F1510, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Buttress_V1\\BS_Buttress_V1.mdl" }, // 16x Buttress
    { 0x70851000, "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Price_List\\ESA_Shop_Price_List.mdl" }, // 16x ShopPriceList
    { 0x74243E2C, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shipping_Crate_V1\\ESA_Shipping_Crate_V1.mdl" }, // 16x ShippingCrate2Lid
    { 0xC6A36E8A, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Abacus\\BS_Abacus.mdl" }, // 16x Abacus
    { 0xDA1D1A52, "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Wall_Plain\\Cellar_Wall_Plain.mdl" }, // 16x Cellar_Wall_Plain
    { 0x84E92C27, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Table_Decorative\\ESA_F_Table_Decorative.mdl" }, // 15x Table_LargeSquare_Upgradeable
    { 0x2D2D6A67, "Art\\Environment\\Shared assets\\Walls\\dotXSI\\Stone_Wall_Medium_Curved_Spiked\\Stone_Wall_Medium_Curved_Spiked.mdl" }, // 14x NewObejctFurnitureSmallWallCorner
    { 0x4D389FBE, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Stove_Normal\\ESA_F_Stove_Normal.mdl" }, // 14x Stove_Normal
    { 0x5F438531, "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Covered_Crate\\ESA_Shop_Covered_Crate.mdl" }, // 14x Covered_Crate
    { 0x89889C3B, "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Shelf_Small\\ESA_Shop_Shelf_Small.mdl" }, // 14x Shop_Shelf_Small
    { 0x8CE52E9F, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_HatStand\\BS_HatStand.mdl" }, // 14x NewHatstand
    { 0x95FE062D, "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Farm_Cupboard\\BW_Farm_Cupboard.mdl" }, // 14x NewObjectFurnitureFarmCupboard
    { 0xB5688CA9, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shelf_Medium\\ESA_Shelf_Medium.mdl" }, // 14x Shelf_Medium
    { 0xBD0F02F6, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Fireplace_Bucket\\ESA_Fireplace_Bucket.mdl" }, // 14x FireplaceBucket
    { 0xD6F0A6E9, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_PictureFrame4\\ESA_PictureFrame4.mdl" }, // 14x ObjectFurniturePictureFrame4
    { 0xF8586952, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Dresser_UltraDecorative\\ESA_F_Dresser_UltraDecorative.mdl" }, // 14x Dresser_Upgradeable
    { 0xD306D0F9, "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Small_Room\\Cellar_Small_Room.mdl" }, // 13x Cellar_Small_Room
    { 0x03AB4C39, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Books_Block_V2\\ESA_Books_Block_V2.mdl" }, // 12x Books_Group_V3
    { 0x1D242882, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Tower\\BS_Market_Wall_Tower.mdl" }, // 12x NewObjectBuildingBSMarketWallTower
    { 0x351F7BC0, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Docks_Arch\\BS_Market_Docks_Arch.mdl" }, // 12x NewObjectBuildingBSMarketDock
    { 0x3E7DB724, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_PictureFrame5\\ESA_PictureFrame5.mdl" }, // 12x ObjectFurniturePictureFrame5
    { 0x7002F0BA, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Cask_Small_V1\\ESA_Cask_Small_V1.mdl" }, // 12x Cask_Small
    { 0x8D3BF70B, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Drawers_UltraDecorative\\ESA_F_Drawers_UltraDecorative.mdl" }, // 12x Drawers_UltraDecorative
    { 0x98D2497D, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_BedWarmer\\ESA_BedWarmer.mdl" }, // 12x Bedwarmer
    { 0x9BD20683, "Art\\Environment\\Shared assets\\Doors_Windows\\dotXSI\\ESA_SmArchedWin_V1\\ESA_SmArchedWin_V1.mdl" }, // 12x NewWindow_Small_Arched
    { 0xD9A92FE5, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_PictureFrame2\\ESA_PictureFrame2.mdl" }, // 12x ObjectFurniturePictureFrame2
    { 0xF0306F13, "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Covered_Table\\ESA_Shop_Covered_Table.mdl" }, // 12x Covered_Table
    { 0x3492784D, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Light_Ceiling\\BS_Light_Ceiling.mdl" }, // 10x LightFixing_Ceiling
    { 0x8DCCB5BC, "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Banner_V3\\ESA_Shop_Banner_V3.mdl" }, // 10x ShopBanner_V3
    { 0xACEC3985, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shelf_Medium\\ESA_Shelf_Medium.mdl" }, // 10x Shelf_Medium
    { 0xB43D159A, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Table_Decorative\\ESA_F_Table_Decorative.mdl" }, // 10x Table_Standard_Upgradeable
    { 0xB9F514CA, "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Shelf_Small\\ESA_Shop_Shelf_Small.mdl" }, // 10x shop_shelf
    { 0xD27AC2A1, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shelf_Medium\\ESA_Shelf_Medium.mdl" }, // 10x Shelf_Medium
    { 0xE210D815, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Books_Block_V2\\ESA_Books_Block_V2.mdl" }, // 10x Books_Group_V6
    { 0x0E09D031, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Books_Block_V2\\ESA_Books_Block_V2.mdl" }, // 8x Books_Group_V6
    { 0x18E72EE1, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Fireplace_Grate\\ESA_Fireplace_Grate.mdl" }, // 8x ObjectFurnitureFireplaceGrate
    { 0x2A621E16, "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Trapdoor_External\\Cellar_Trapdoor_External.mdl" }, // 8x Cellar_Trapdoor_External
    { 0x327C7D01, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_HatStand\\BS_HatStand.mdl" }, // 8x Hatstand
    { 0x3724C4F1, "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_WallClock\\BS_WallClock.mdl" }, // 8x WallClock
    { 0x68DD7F94, "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Corridor_Stair_Corner\\Cellar_Corridor_Stair_Corner.mdl" }, // 8x Cellar_Corridor_Stair_Corner
    { 0x7046E366, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Guardpost\\BS_Market_Guardpost.mdl" }, // 8x NewObjectBuildingBSGuardPost
    { 0x748A3051, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Slope\\BS_Market_Wall_Slope.mdl" }, // 8x NewObjectBuildingBSMarketWallSlope
    { 0x89682293, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Docks_Wall\\BS_Market_Docks_Wall.mdl" }, // 8x BS_Docks_Wall_Buttress
    { 0x9A2E51C6, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Gate\\BS_Market_Wall_Gate.mdl" }, // 8x BowerstoneWall_Closed_Gate
    { 0xAE711422, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Workbench\\ESA_Workbench.mdl" }, // 8x Workbench
    { 0xC6177B50, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Cask_Large_V2\\ESA_Cask_Large_V2.mdl" }, // 8x Cask_Large_V2
    { 0xD9AD9309, "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Dresser_UltraDecorative\\ESA_F_Dresser_UltraDecorative.mdl" }, // 8x Dresser_Upgradeable
    { 0xFDD6D925, "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Easel\\ESA_Easel.mdl" }, // 8x Easel
    { 0xA214008A, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_LockGates\\BS_Market_LockGates.mdl" }, // 4x NewObjectBuildingBSLockgate
    { 0x047E48AE, "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_Market_Gatehouse\\BS_Market_Gatehouse.mdl" }, // 2x NewObjectBuildingBSGateHouseRear
    { 0xB7C6F3FD, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Bridge\\BS_Market_Bridge.mdl" }, // 2x NewObjectBuildingBSMarketBridge
    { 0xD55304DB, "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_ClockTower\\BS_Market_ClockTower.mdl" }, // 2x QO700_ClockTowerBase
};

// Base entity keys after stripping trailing digits. These catch numbered
// instances where the parent hash is unknown or intentionally omitted.
constexpr KeyOverride kEntityKeyOverrides[] = {
    { "bsmarkettownhousesmall", "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_TownHouse_Basic_Facade_Mid\\BS_TownHouse_Basic_Facade_Mid.mdl" },
    { "booksgroupv", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Books_Block_V2\\ESA_Books_Block_V2.mdl" },
    { "esamdboxwin", "Art\\Environment\\Shared assets\\Doors_Windows\\dotXSI\\ESA_MdBoxWin\\ESA_MdBoxWin\\ESA_MdBoxWin.mdl" },
    { "lightfixingceiling", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Light_Ceiling\\BS_Light_Ceiling.mdl" },
    { "shippingcrate", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shipping_Crate_V1\\ESA_Shipping_Crate_V1.mdl" },
    { "smallwallstraight", "Art\\Environment\\Shared assets\\Walls\\dotXSI\\Stone_Wall_Medium_Straight_Spiked\\Stone_Wall_Medium_Straight_Spiked.mdl" },
    { "smallwallpost", "Art\\Environment\\Shared assets\\Walls\\dotXSI\\Stone_Wall_Medium_Post_Spiked\\Stone_Wall_Medium_Post_Spiked.mdl" },
    { "candleholder", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_CandleHolder\\BS_CandleHolder.mdl" },
    { "shelflong", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shelf_Long\\ESA_Shelf_Long.mdl" },
    { "oillanternsingle", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Cemetary_OilLamp_Single\\BS_Cemetary_OilLamp_Single.mdl" },
    { "staticbwtowerrailings", "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Tower_Railings\\BW_Tower_Railings.mdl" },
    { "shelfmedium", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shelf_Medium\\ESA_Shelf_Medium.mdl" },
    { "slumstreethouse", "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_TownHouse_Basic_Facade_Mid\\BS_TownHouse_Basic_Facade_Mid.mdl" },
    { "grainsack", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Sack_Grain\\ESA_Sack_Grain.mdl" },
    { "esatismboxwin", "Art\\Environment\\Shared assets\\Doors_Windows\\dotXSI\\ESA_TISmBoxWin\\ESA_TISmBoxWin\\ESA_TISmBoxWin.mdl" },
    { "dresserupgradeable", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Dresser_UltraDecorative\\ESA_F_Dresser_UltraDecorative.mdl" },
    { "tablelargesquareupgradeable", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Table_Decorative\\ESA_F_Table_Decorative.mdl" },
    { "comfychair", "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Tower_ComfyChair\\BW_Tower_ComfyChair.mdl" },
    { "goblet", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Goblet\\ESA_Goblet.mdl" },
    { "fireplacegrate", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Fireplace_Grate\\ESA_Fireplace_Grate.mdl" },
    { "bsmarketdockarch", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Docks_Arch\\BS_Market_Docks_Arch.mdl" },
    { "bsmarketwalljoiner", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Buffer\\BS_Market_Wall_Buffer.mdl" },
    { "bookcaseultradecorative", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Bookcase_UltraDecorative\\ESA_F_Bookcase_UltraDecorative.mdl" },
    { "tablestandardupgradeable", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Table_Decorative\\ESA_F_Table_Decorative.mdl" },
    { "quill", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Quill\\ESA_Quill.mdl" },
    { "bwtowerrailings10slope", "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Tower_Railings\\BW_Tower_Railings.mdl" },
    { "bsdockwall", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Docks_Wall\\BS_Market_Docks_Wall.mdl" },
    { "generalstore", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Sign_GeneralStore\\ESA_Sign_GeneralStore.mdl" },
    { "pubtable", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Table_Tavern\\ESA_Table_Tavern.mdl" },
    { "hatstand", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_HatStand\\BS_HatStand.mdl" },
    { "grandfatherclock", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_GrandfatherClock\\BS_GrandfatherClock.mdl" },
    { "hangingbasket", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_HangingBasket\\ESA_HangingBasket.mdl" },
    { "mannequintorso", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_MannequinTorso\\BS_MannequinTorso.mdl" },
    { "cellarlargeroom", "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Large_Room\\Cellar_Large_Room.mdl" },
    { "pisspot", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Pisspot\\ESA_Pisspot.mdl" },
    { "bsmarketwallstraight", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Straight\\BS_Market_Wall_Straight.mdl" },
    { "till", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Till\\BS_Till.mdl" },
    { "tablelargesquareultradecorative", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Table_UltraDecorative\\ESA_F_Table_UltraDecorative.mdl" },
    { "shippingcrate3lid", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shipping_Crate_V1\\ESA_Shipping_Crate_V1.mdl" },
    { "bookcasenormal", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Bookcase_Normal\\ESA_F_Bookcase_Normal.mdl" },
    { "haybaleblock", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_HayBale_Block\\ESA_HayBale_Block.mdl" },
    { "bsslumswall", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Slums_ThinWall_V1\\BS_Slums_ThinWall_V1.mdl" },
    { "fireplacebucket", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Fireplace_Bucket\\ESA_Fireplace_Bucket.mdl" },
    { "tablestandardultradecorative", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Table_UltraDecorative\\ESA_F_Table_UltraDecorative.mdl" },
    { "buntingvertical", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_BuntingVertical\\BS_BuntingVertical.mdl" },
    { "vase", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Vase\\BS_Vase.mdl" },
    { "chairwoodenultradecorative", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Chair_Wooden_UltraDecorative\\ESA_F_Chair_Wooden_UltraDecorative.mdl" },
    { "casksmall", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Cask_Small_V1\\ESA_Cask_Small_V1.mdl" },
    { "abacus", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Abacus\\BS_Abacus.mdl" },
    { "spiritbottle", "Art\\Inventory\\Objects\\dotXSI\\OB_SpiritBottle\\OB_SpiritBottle.mdl" },
    { "wallclock", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_WallClock\\BS_WallClock.mdl" },
    { "easel", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Easel\\ESA_Easel.mdl" },
    { "buttress", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Buttress_V1\\BS_Buttress_V1.mdl" },
    { "tablestandardworn", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Table_UltraDecorative\\ESA_F_Table_UltraDecorative.mdl" },
    { "cellarwallplain", "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Wall_Plain\\Cellar_Wall_Plain.mdl" },
    { "shippingcrate2lid", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Shipping_Crate_V1\\ESA_Shipping_Crate_V1.mdl" },
    { "shoppricelist", "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Price_List\\ESA_Shop_Price_List.mdl" },
    { "bedwarmer", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_BedWarmer\\ESA_BedWarmer.mdl" },
    { "cellarcorridorstairs", "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Corridor_Stairs\\Cellar_Corridor_Stairs.mdl" },
    { "bsmarketsmallshop", "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_Market_SmallShop_Facade\\BS_Market_SmallShop_Facade.mdl" },
    { "jug", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Jug\\BS_Jug.mdl" },
    { "fireplacepoker", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Fireplace_Poker\\ESA_Fireplace_Poker.mdl" },
    { "dresserultradecorative", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Dresser_UltraDecorative\\ESA_F_Dresser_UltraDecorative.mdl" },
    { "coveredcrate", "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Covered_Crate\\ESA_Shop_Covered_Crate.mdl" },
    { "farmcupboard", "Art\\Environment\\Regions\\Brightwood\\Props\\dotXSI\\BW_Farm_Cupboard\\BW_Farm_Cupboard.mdl" },
    { "shopshelfsmall", "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Shelf_Small\\ESA_Shop_Shelf_Small.mdl" },
    { "obejctfurnituresmallwallcorner", "Art\\Environment\\Shared assets\\Walls\\dotXSI\\Stone_Wall_Medium_Curved_Spiked\\Stone_Wall_Medium_Curved_Spiked.mdl" },
    { "cellarsmallroom", "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Small_Room\\Cellar_Small_Room.mdl" },
    { "esasmarchedwinv", "Art\\Environment\\Shared assets\\Doors_Windows\\dotXSI\\ESA_SmArchedWin_V1\\ESA_SmArchedWin_V1.mdl" },
    { "bsmarketdock", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Docks_Arch\\BS_Market_Docks_Arch.mdl" },
    { "bsmarketwalltower", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Tower\\BS_Market_Wall_Tower.mdl" },
    { "coveredtable", "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Covered_Table\\ESA_Shop_Covered_Table.mdl" },
    { "artistsbrush", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Artists_Brush\\ESA_Artists_Brush.mdl" },
    { "windowsmallarched", "Art\\Environment\\Shared assets\\Doors_Windows\\dotXSI\\ESA_SmArchedWin_V1\\ESA_SmArchedWin_V1.mdl" },
    { "toybox", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Toy_Box\\ESA_Toy_Box.mdl" },
    { "cupboardultradecorative", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Cupboard_UltraDecorative\\ESA_F_Cupboard_UltraDecorative.mdl" },
    { "drawersultradecorative", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Drawers_UltraDecorative\\ESA_F_Drawers_UltraDecorative.mdl" },
    { "artistspalette", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Artists_Palette\\ESA_Artists_Palette.mdl" },
    { "mug", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Mug_V2\\ESA_Mug_V2.mdl" },
    { "statictowerstove", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Stove_Normal\\ESA_F_Stove_Normal.mdl" },
    { "cratewine", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Crate_Wine\\ESA_Crate_Wine\\ESA_Crate_Wine.mdl" },
    { "cellarcorridorstaircorner", "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Corridor_Stair_Corner\\Cellar_Corridor_Stair_Corner.mdl" },
    { "cellartrapdoorexternal", "Art\\Environment\\Regions\\Dungeon\\Tombs\\Pieces\\dotXSI\\Cellar_Trapdoor_External\\Cellar_Trapdoor_External.mdl" },
    { "mannequinhead", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_MannequinHead\\BS_MannequinHead.mdl" },
    { "workbench", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_Workbench\\ESA_Workbench.mdl" },
    { "qo060gargoylestatue", "Art\\Environment\\Shared assets\\Old Kingdom\\dotXSI\\OK_Statue_Dolphin_V1\\OK_Statue_Dolphin_V1.mdl" },
    { "staticwoodenbucket", "Art\\Environment\\Shared assets\\Props\\dotXSI\\ESA_BucketWooden\\ESA_BucketWooden.mdl" },
    { "bowerstonewallclosedgate", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Gate\\BS_Market_Wall_Gate.mdl" },
    { "bsguardpost", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Guardpost\\BS_Market_Guardpost.mdl" },
    { "vasev", "Art\\Environment\\Regions\\Bowerstone\\Props\\dotXSI\\BS_Vase_V2\\BS_Vase_V2.mdl" },
    { "bookcaseworn", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Bookcase_Worn\\ESA_F_Bookcase_Worn.mdl" },
    { "bsmarketwallslope", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Wall_Slope\\BS_Market_Wall_Slope.mdl" },
    { "bsdockswallbuttress", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Docks_Wall\\BS_Market_Docks_Wall.mdl" },
    { "chairwoodenupgradeable", "Art\\Environment\\Shared assets\\Furniture\\dotXSI\\ESA_F_Chair_Wooden_UltraDecorative\\ESA_F_Chair_Wooden_UltraDecorative.mdl" },
    { "weaponrack", "Art\\Environment\\Shared assets\\ShopClutter\\dotXSI\\ESA_Shop_Weapon_Rack\\ESA_Shop_Weapon_Rack.mdl" },
    { "bsmarketbridge", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_Bridge\\BS_Market_Bridge.mdl" },
    { "qo700clocktowerbase", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_ClockTower\\BS_Market_ClockTower.mdl" },
    { "bslockgate", "Art\\Environment\\Regions\\Bowerstone\\Structures\\dotXSI\\BS_Market_LockGates\\BS_Market_LockGates.mdl" },
    { "bsgatehouserear", "Art\\Environment\\Regions\\Bowerstone\\Buildings\\dotXSI\\BS_Market_Gatehouse\\BS_Market_Gatehouse.mdl" },
};

std::string StripTrailingDigits(std::string key)
{
    while (!key.empty() &&
           std::isdigit(static_cast<unsigned char>(key.back()))) {
        key.pop_back();
    }
    return key;
}

const char* LookupEntityKeyExact(const std::string& key)
{
    for (const auto& row : kEntityKeyOverrides) {
        if (key == row.key) return row.model_path;
    }
    return nullptr;
}

}

const char* LookupParentHash(uint32_t parent_hash)
{
    if (parent_hash == 0) return nullptr;
    for (const auto& row : kParentHashOverrides) {
        if (parent_hash == row.hash) return row.model_path;
    }
    return nullptr;
}

const char* LookupEntityKey(const std::string& entity_key)
{
    if (entity_key.empty()) return nullptr;
    if (const char* hit = LookupEntityKeyExact(entity_key)) return hit;

    std::string base = StripTrailingDigits(entity_key);
    if (base != entity_key) {
        return LookupEntityKeyExact(base);
    }
    return nullptr;
}

}
