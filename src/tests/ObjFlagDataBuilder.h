#ifndef OBJFLAGDATABUILDER_H_
#define OBJFLAGDATABUILDER_H_

#include "../structs.h"


namespace builders {

    class ObjFlagDataBuilder {

    private:
        // Value-initialized ({}): obj_flag_data has no constructor or member
        // initializers, so a default-initialized instance leaves every field
        // its fluent setters don't touch indeterminate -- accidentally zero
        // on glibc/libc++ builds, but 0xCC-filled (-858993460) on MSVC Debug,
        // which broke two ActInfoObjectId characterization pins at Wave 3
        // finalization CI. Zeroing here gives every built obj_flag_data a
        // deterministic all-zero baseline before the setters overlay the
        // intended fields, for every existing and future fixture at once.
        obj_flag_data data {};

    public:
        ObjFlagDataBuilder() {
        }

        ObjFlagDataBuilder &setObCoef(int value);

        ObjFlagDataBuilder &setParryCoef(int value);

        ObjFlagDataBuilder &setBulk(int value);

        ObjFlagDataBuilder &setLevel(unsigned char value);

        ObjFlagDataBuilder &setWeight(int value);

        ObjFlagDataBuilder &setWeaponType(game_types::weapon_type value);

        ObjFlagDataBuilder &setMaterial(signed char value);

        ObjFlagDataBuilder &setWearFlags(int value);

        ObjFlagDataBuilder &setExtraFlags(int value);

        ObjFlagDataBuilder &setCost(int value);

        ObjFlagDataBuilder &setCostPerDay(signed short int value);

        ObjFlagDataBuilder &setTimer(int value);

        ObjFlagDataBuilder &setBitVector(int value);

        ObjFlagDataBuilder &setRarity(unsigned char value);

        ObjFlagDataBuilder &setButcherItem(signed short int value);

        ObjFlagDataBuilder &setProgramNumber(int value);

        ObjFlagDataBuilder &setScriptNumber(int value);

        ObjFlagDataBuilder &setScriptInfo(info_script *script_info);

        obj_flag_data build() const {
            return data;
        }
    };
}

#endif //OBJFLAGDATABUILDER_H_