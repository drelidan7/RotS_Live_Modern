#pragma once
// rots/core/fwd.h — forward declarations of the core entities (spec §5).
// Entity headers include THIS, never each other: entities reference one
// another only by pointer, so full definitions are pulled in only by the
// .cpp files (and headers) that actually dereference members.
struct char_data;
struct obj_data;
struct room_data;
struct descriptor_data;
struct txt_block; // defined in rots/core/types.h; held by pointer in target_data
