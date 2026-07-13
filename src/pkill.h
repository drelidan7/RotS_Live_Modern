#ifndef __PKILL_H__
#define __PKILL_H__

#include <string>
#include <string_view> // Bounded JSON and decoder labels do not require terminators.
#include <vector>

/*
 * XXX: kill_time should be time_t, killer and victim should be
 * longs.  These cannot be changed without updating the binary
 * pkill file, but this SHOULD be done.
 *
 * These values are stored in the file as idnums, but are
 * converted to player table indexes when the file is read.
 */
typedef struct {
    int kill_time;
    int killer;
    int victim;
    unsigned char killer_level;
    unsigned char victim_level;
    int killer_points;
    int victim_points; /* Victim points are stored as negative values */
} PKILL;

void boot_pkills();
void pkill_create(struct char_data*);

// Phase 2a Task 6: pkill persistence as JSON (see pkill.cpp for the
// implementation). Declared here (rather than kept file-local) so the
// pod_persistence_json_tests.cpp TU can exercise the codec + converter
// directly against synthesized legacy fixtures, matching the
// exploits_json.h/mail_json (in mail.h) precedent.
namespace pkill_json {

static constexpr int PKILL_SCHEMA_VERSION = 1;

struct PkillStoreData {
    int version = PKILL_SCHEMA_VERSION;
    std::vector<PKILL> records;
};

bool legacy_pkill_file_from_binary(const std::string& bytes, std::vector<PKILL>* records, std::string* error_message = nullptr);
std::string serialize_pkill_to_json(const PkillStoreData& data);
/// Deserializes bounded player-kill JSON, stopping at its first embedded null byte.
bool deserialize_pkill_from_json(std::string_view json, PkillStoreData *data, std::string *error_message = nullptr);
bool pkill_records_equal(const std::vector<PKILL>& a, const std::vector<PKILL>& b);
std::string pkill_json_path(const std::string& legacy_path);
bool convert_legacy_pkill_file(const char* legacy_path, std::string* error_message = nullptr);

} // namespace pkill_json

void pkill_unref_character(struct char_data* c);
void pkill_unref_character_by_index(int);

int pkill_get_total();

#define PKILL_STRING_KILLED 0
#define PKILL_STRING_SLAIN 1
char* pkill_get_string(PKILL*, int);

PKILL* pkill_get_new_kills(int*);

/* XXX: Should eventually be removed */
PKILL* pkill_get_all(int*);

int pkill_get_good_fame();
int pkill_get_evil_fame();

typedef struct {
    char* name;
    int player_idx;
    int rank;
    int fame;
    int race;
    int side;
    int invalid; /* Non-zero means this leader was invalid */
} LEADER;

#define PKILL_UNRANKED -1
LEADER* pkill_get_leader_by_rank(int, int);
void pkill_free_leader(LEADER*);

int pkill_get_rank_by_character(struct char_data* c, bool totalRank);
int pkill_get_totalrank_by_character_id(int idx, bool totalRank);

#endif /* __PKILL_H__ */
