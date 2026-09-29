#include <catch2/catch_all.hpp>

#include "slic3r/Utils/PrintHost.hpp"
#include "slic3r/Utils/Flashforge.hpp"
#include "slic3r/GUI/UserManager.hpp"

using namespace Slic3r;

TEST_CASE("PrintHost::get_err_code_from_body reports a non-JSON reply as an error", "[PrintHost]") {
    CHECK(PrintHost::get_err_code_from_body(R"({"err": 3})") == 3);
    CHECK(PrintHost::get_err_code_from_body(R"({"err": 0})") == 0);
    CHECK(PrintHost::get_err_code_from_body(R"({"status": "ok"})") == 0);
    CHECK(PrintHost::get_err_code_from_body("<html><body>Not found</body></html>") == -1);
    CHECK(PrintHost::get_err_code_from_body(R"({"err":)") == -1);
    CHECK(PrintHost::get_err_code_from_body("") == -1);
}

TEST_CASE("UserManager::parse_json reports a non-JSON payload instead of throwing", "[PrintHost]") {
    UserManager user_manager(nullptr);
    CHECK(user_manager.parse_json("<html>Not found</html>") == -1);
    CHECK(user_manager.parse_json(R"({"unexpected": true})") == -1);
}

TEST_CASE("Flashforge::parse_material_slots tolerates malformed material data", "[PrintHost]") {
    std::vector<FlashforgeMaterialSlot> slots;
    bool                                supports_material_station = false;

    SECTION("a non-JSON reply is reported, not parsed") {
        CHECK_FALSE(Flashforge::parse_material_slots("<html>Not found</html>", slots, &supports_material_station));
        CHECK(slots.empty());
    }
    SECTION("off-type fields and non-object entries are skipped") {
        const std::string body = R"({"detail": {"hasMatlStation": "1", "matlStationInfo": {"slotCnt": 2,
            "slotInfos": [7, {"slotId": "3", "hasFilament": "1", "materialName": 5, "materialColor": "FF0000"}]}}})";
        REQUIRE(Flashforge::parse_material_slots(body, slots, &supports_material_station));
        CHECK(supports_material_station);
        REQUIRE(slots.size() == 1);
        CHECK(slots[0].slot_id == 3);
        CHECK(slots[0].has_filament);
        CHECK(slots[0].material_name.empty());
        CHECK(slots[0].material_color == "FF0000");
    }
    SECTION("a valid detail reply still parses") {
        const std::string body = R"({"detail": {"matlStationInfo": {"slotInfos": [
            {"slotId": 1, "hasFilament": 1, "materialName": "PLA", "materialColor": "00FF00"}]}}})";
        REQUIRE(Flashforge::parse_material_slots(body, slots, &supports_material_station));
        CHECK(supports_material_station);
        REQUIRE(slots.size() == 1);
        CHECK(slots[0].slot_id == 1);
        CHECK(slots[0].has_filament);
        CHECK(slots[0].material_name == "PLA");
        CHECK(slots[0].material_color == "00FF00");
    }
}
