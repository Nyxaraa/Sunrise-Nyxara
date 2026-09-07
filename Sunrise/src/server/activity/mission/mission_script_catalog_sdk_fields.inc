#define CATALOG_U32(lua_key, member)                                                               \
    if (key == (lua_key)) return field_u32(output, member);
#define CATALOG_I32(lua_key, member)                                                               \
    if (key == (lua_key)) return field_i32(output, member);
#define CATALOG_U64(lua_key, member)                                                               \
    if (key == (lua_key)) return field_u64(output, member);
#define CATALOG_I64(lua_key, member)                                                               \
    if (key == (lua_key)) return field_i64(output, member);
#define CATALOG_STRING(lua_key, member)                                                            \
    if (key == (lua_key)) return field_string(output, catalog, member);                            \
    if (key == lua_key "_string_offset") return field_u32(output, (member).offset);                \
    if (key == lua_key "_string_length") return field_u32(output, (member).length);
#define CATALOG_RANGE(lua_key, member)                                                             \
    if (key == lua_key "_first_index") return field_u32(output, (member).first);                   \
    if (key == lua_key "_count") return field_u32(output, (member).count);
#define CATALOG_BYTES(lua_key, member)                                                             \
    if (key == (lua_key)) return field_bytes(output, member);
