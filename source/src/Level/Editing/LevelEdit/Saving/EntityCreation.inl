uint32_t create_entity_addition(GdbEdit::GdbFile& g,
                                const Addition& a,
                                std::unordered_map<uint32_t, std::string>&
                                    babel_edits,
                                std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kNull = 0x811C9DC5u;
    constexpr uint32_t kVecX = 0x050C5D47u;
    constexpr uint32_t kVecY = 0x050C5D46u;
    constexpr uint32_t kVecZ = 0x050C5D45u;
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kRotation = 0x21EBC83Bu;
    constexpr uint32_t kKeyframed = 0x6B177DD0u;
    constexpr uint32_t kDynamic = 0xFC8A57C5u;

    constexpr uint32_t kChestTemplate = 0x8C13FB53u;
    constexpr uint32_t kChestTemplateKeyframed = 0x9AB9A90Cu;
    constexpr uint32_t kChestPositionTemplate = 0x2CA65C69u;
    constexpr uint32_t kChestRotationTemplate = 0xC10576FAu;
    constexpr uint32_t kChestBaseTemplate = 0x3CABC379u;
    constexpr uint32_t kChestGraphicBase = 0x8AAF44E3u;
    constexpr uint32_t kChestComponentBase = 0x8DD05F71u;
    constexpr uint32_t kChestSkeleton = 0xF94F44C0u;
    constexpr uint32_t kGraphicAppearanceAnimatedMesh = 0x21D312CAu;
    constexpr uint32_t kChestComponent = 0x379C25A9u;
    constexpr uint32_t kObjectComponent = 0xF1A5EEB9u;
    constexpr uint32_t kModelFile = 0x0C17DB4Eu;
    constexpr uint32_t kSkeletonFile = 0xC3D06E3Au;
    constexpr uint32_t kSilverKeysNeeded = 0xB208E419u;
    constexpr uint32_t kObjectComponentBase = 0xA4EB5624u;
    constexpr uint32_t kMaterial = 0x6D04D9A2u;
    constexpr uint32_t kSilverChestMaterial = 0xE294B870u;

    constexpr uint32_t kSilverKeyTemplate = 0x4AB4C31Au;
    constexpr uint32_t kSilverKeyTemplateDynamic = 0xEA7C60E5u;
    constexpr uint32_t kSilverKeyPositionTemplate = 0xEF585A66u;
    constexpr uint32_t kSilverKeyRotationTemplate = 0x9BB57EABu;

    const bool is_key = a.entity_kind == AdditionEntityKind::SilverKey;
    const bool is_prop = a.entity_kind == AdditionEntityKind::GenericProp;
    const bool is_npc = a.entity_kind == AdditionEntityKind::Npc;
    const bool is_silver_key_chest =
        a.entity_kind == AdditionEntityKind::Chest &&
        a.silver_keys_needed > 0;



    const bool author_silver_chest_graphics = is_silver_key_chest;
    uint32_t entity_template =
        is_key ? kSilverKeyTemplate : kChestTemplate;
    uint32_t comp_field = is_key ? kDynamic : kKeyframed;
    uint32_t comp_template =
        is_key ? kSilverKeyTemplateDynamic : kChestTemplateKeyframed;
    if (is_prop || is_npc) {
        if (!a.entity_template || !a.entity_comp_field) {
            err = is_npc ? "NPC entity missing placement template info"
                         : "prop entity missing template info";
            return 0;
        }
        entity_template = a.entity_template;
        comp_field = a.entity_comp_field;
        comp_template = a.entity_comp_template;
    } else if (a.entity_kind == AdditionEntityKind::Chest &&
               !is_silver_key_chest &&
               a.entity_template && a.entity_comp_field) {
        entity_template = a.entity_template;
        comp_field = a.entity_comp_field;
        comp_template = a.entity_comp_template;
    }
    uint32_t position_template = a.entity_position_template;
    uint32_t rotation_template = a.entity_rotation_template;
    GdbEdit::Field template_field;
    if ((!position_template || !rotation_template) &&
        comp_template && comp_template != kNull) {
        if (g.FindLocalField(comp_template, kPosition, template_field) &&
            template_field.type == 6) {
            position_template = template_field.value;
        }
        if (g.FindLocalField(comp_template, kRotation, template_field) &&
            template_field.type == 6) {
            rotation_template = template_field.value;
        }
    }
    if (is_key) {
        if (!position_template) {
            position_template = kSilverKeyPositionTemplate;
        }
        if (!rotation_template) {
            rotation_template = kSilverKeyRotationTemplate;
        }
    } else if (a.entity_kind == AdditionEntityKind::Chest &&
               (a.entity_name.empty() || is_silver_key_chest)) {
        if (!position_template) position_template = kChestPositionTemplate;
        if (!rotation_template) rotation_template = kChestRotationTemplate;
    }

    auto fbits = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    };

    auto vec3_record = [&](float x, float y, float z,
                           uint32_t parent) -> uint32_t {
        const uint32_t h = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kVecZ; f.type = 3; f.value = fbits(z); f.decl = 3;
        fs.push_back(f);
        f.hash = kVecY; f.type = 3; f.value = fbits(y); f.decl = 2;
        fs.push_back(f);
        f.hash = kVecX; f.type = 3; f.value = fbits(x); f.decl = 1;
        fs.push_back(f);
        if (parent) {
            f.hash = kParent; f.type = 6; f.value = parent; f.decl = 0;
            fs.push_back(f);
        }
        return g.AddRecord(h, fs, 1) ? h : 0;
    };

    const uint32_t pos_rec = vec3_record(
        a.pos[0], a.pos[1], a.pos[2], position_template);
    const float yaw = a.yaw_deg * 0.01745329252f;
    const uint32_t rot_rec = vec3_record(
        yaw, 0.0f, 0.0f, rotation_template);
    if (!pos_rec || !rot_rec) {
        err = "transform record append failed";
        return 0;
    }

    const uint32_t comp_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kRotation; f.type = 6; f.value = rot_rec; f.decl = 1;
        fs.push_back(f);
        if (comp_template && comp_template != kNull) {
            f.hash = kParent; f.type = 6; f.value = comp_template;
            f.decl = 2;
            fs.push_back(f);
        }
        f.hash = kPosition; f.type = 6; f.value = pos_rec; f.decl = 0;
        fs.push_back(f);
        if (!g.AddRecord(comp_rec, fs, 1)) {
            err = "transform component append failed";
            return 0;
        }
    }

    uint32_t tags_rec = 0;
    uint32_t text_tag = 0;
    if (is_prop && a.entity_has_text && !a.readable_text.empty()) {
        constexpr uint32_t kTextTag = 0xB8F45248u;
        text_tag = TextBank::AllocTagHash(
            lower_model_path(a.model_path) + "#f2ab_text");
        while (babel_edits.count(text_tag) != 0 || text_tag == 0) {
            ++text_tag;
        }
        tags_rec = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kTextTag; f.type = 4; f.value = text_tag;
        f.decl = 0;
        fs.push_back(f);
        if (!g.AddRecord(tags_rec, fs, 1)) {
            err = "readable component record append failed";
            return 0;
        }
    }

    uint32_t graphic_rec = 0;
    if (author_silver_chest_graphics) {
        const std::string model_path = lower_model_path(a.model_path);
        const uint32_t model_hash = fnv1_32(model_path);
        graphic_rec = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kModelFile; f.type = 4; f.value = model_hash; f.decl = 1;
        fs.push_back(f);
        f.hash = kParent; f.type = 6; f.value = kChestGraphicBase;
        f.decl = 0;
        fs.push_back(f);
        f.hash = kSkeletonFile; f.type = 4; f.value = kChestSkeleton;
        f.decl = 2;
        fs.push_back(f);
        if (!g.AddRecord(graphic_rec, fs, 1)) {
            err = "silver-key chest graphics append failed";
            return 0;
        }
        g.AddDictString(model_hash, model_path);
    }

    uint32_t chest_rec = 0;
    if (author_silver_chest_graphics) {
        chest_rec = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = kChestComponentBase;
        f.decl = 0;
        fs.push_back(f);
        f.hash = kSilverKeysNeeded; f.type = 1;
        f.value = uint32_t(a.silver_keys_needed);
        f.decl = 1;
        fs.push_back(f);
        if (!g.AddRecord(chest_rec, fs, 1)) {
            err = "silver-key chest lock append failed";
            return 0;
        }
    }

    uint32_t object_rec = 0;
    if (author_silver_chest_graphics) {
        object_rec = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = kObjectComponentBase;
        f.decl = 0; fs.push_back(f);
        f.hash = kMaterial; f.type = 7; f.value = kSilverChestMaterial;
        f.decl = 1; fs.push_back(f);
        if (!g.AddRecord(object_rec, fs, 1)) {
            err = "silver-key chest object component append failed";
            return 0;
        }
    }






    if (author_silver_chest_graphics) {
        const uint32_t silver_chest_template = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kGraphicAppearanceAnimatedMesh;
        f.type = 6; f.value = graphic_rec; f.decl = 2; fs.push_back(f);
        f.hash = kChestComponent;
        f.type = 6; f.value = chest_rec; f.decl = 0; fs.push_back(f);
        f.hash = kParent;
        f.type = 6; f.value = kChestBaseTemplate; f.decl = 1;
        fs.push_back(f);
        f.hash = kKeyframed;
        f.type = 6; f.value = kChestTemplateKeyframed; f.decl = 3;
        fs.push_back(f);
        f.hash = kObjectComponent;
        f.type = 6; f.value = object_rec; f.decl = 4; fs.push_back(f);
        if (!g.AddRecord(silver_chest_template, fs, 0)) {
            err = "silver-key chest template append failed";
            return 0;
        }
        entity_template = silver_chest_template;
    }

    const uint32_t entity_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = entity_template;
        f.decl = 2;
        fs.push_back(f);
        f.hash = comp_field; f.type = 6; f.value = comp_rec;
        f.decl = 0;
        fs.push_back(f);
        if (tags_rec) {
            f.hash = 0x89ABB47Eu; f.type = 6; f.value = tags_rec;
            f.decl = 1;
            fs.push_back(f);
        }
        if (!g.AddRecord(entity_rec, fs, 0)) {
            err = "entity record append failed";
            return 0;
        }
    }
    if (tags_rec && text_tag) {
        babel_edits[text_tag] = a.readable_text;
    }

    if (a.entity_kind == AdditionEntityKind::Chest &&
        !apply_chest_contents(g, entity_rec, a.chest_items,
                              a.loot_table_record, true, err)) {
        return 0;
    }
    return entity_rec;
}
