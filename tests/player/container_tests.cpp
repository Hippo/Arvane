#include "arv/player/container.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

TEST(ContainerRegistryTests, ParsesTypeAndLayoutDefinitions) {
  auto types = arv::player::parse_container_type_definitions_json(R"json(
    {
      "container_types": [
        {
          "id": "arvane:chest",
          "slot_count": 27,
          "title": "Chest",
          "ui_layout": "arvane:chest"
        }
      ]
    }
  )json");
  ASSERT_TRUE(types) << types.error().message;

  auto type_registry =
      arv::player::ContainerTypeRegistry::build(std::move(types.value()));
  ASSERT_TRUE(type_registry) << type_registry.error().message;
  const arv::player::ContainerTypeDefinition* chest =
      type_registry.value().find("arvane:chest");
  ASSERT_NE(chest, nullptr);
  EXPECT_EQ(chest->slot_count, 27U);
  EXPECT_EQ(chest->ui_layout, "arvane:chest");

  auto layouts = arv::player::parse_container_ui_layout_definitions_json(R"json(
    {
      "container_layouts": [
        {
          "id": "arvane:chest",
          "size": [426, 332],
          "slot_groups": [
            {
              "id": "chest",
              "source": "container",
              "start_slot": 0,
              "slot_count": 27,
              "columns": 9,
              "position": [18, 34],
              "slot_spacing": [44, 44]
            },
            {
              "id": "inventory",
              "source": "player_inventory",
              "start_slot": 9,
              "slot_count": 27,
              "columns": 9,
              "position": [18, 180],
              "slot_spacing": [44, 44]
            },
            {
              "id": "hotbar",
              "source": "player_inventory",
              "start_slot": 0,
              "slot_count": 9,
              "columns": 9,
              "position": [18, 288],
              "slot_spacing": [44, 44]
            }
          ]
        }
      ]
    }
  )json");
  ASSERT_TRUE(layouts) << layouts.error().message;

  auto layout_registry =
      arv::player::ContainerUiLayoutRegistry::build(std::move(layouts.value()));
  ASSERT_TRUE(layout_registry) << layout_registry.error().message;
  const arv::player::ContainerUiLayoutDefinition* layout =
      layout_registry.value().find("arvane:chest");
  ASSERT_NE(layout, nullptr);
  ASSERT_EQ(layout->slot_groups.size(), 3U);
  EXPECT_EQ(layout->slot_groups[0].source, arv::player::ContainerSlotSource::OpenedContainer);
  EXPECT_EQ(layout->slot_groups[1].source, arv::player::ContainerSlotSource::PlayerInventory);
}

TEST(ContainerRegistryTests, RejectsDuplicateContainerTypeIds) {
  std::vector<arv::player::ContainerTypeDefinition> definitions;
  definitions.push_back({"arvane:chest", 27, "Chest", "arvane:chest"});
  definitions.push_back({"arvane:chest", 9, "Other", "arvane:other"});

  auto registry = arv::player::ContainerTypeRegistry::build(std::move(definitions));

  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().message.find("duplicate container type id"), std::string::npos);
}

TEST(ContainerRegistryTests, AcceptsNameAsPackMergeIdentifierAlias) {
  auto types = arv::player::parse_container_type_definitions_json(R"json(
    {
      "container_types": [
        {
          "name": "arvane:chest",
          "slot_count": 27,
          "title": "Chest",
          "ui_layout": "arvane:chest"
        }
      ]
    }
  )json");
  ASSERT_TRUE(types) << types.error().message;
  ASSERT_EQ(types.value().size(), 1U);
  EXPECT_EQ(types.value().front().id, "arvane:chest");

  auto layouts = arv::player::parse_container_ui_layout_definitions_json(R"json(
    {
      "container_layouts": [
        {
          "name": "arvane:chest",
          "size": [426, 332],
          "slot_groups": [
            {
              "source": "container",
              "start_slot": 0,
              "slot_count": 27,
              "columns": 9,
              "position": [18, 38],
              "slot_spacing": [44, 44]
            }
          ]
        }
      ]
    }
  )json");
  ASSERT_TRUE(layouts) << layouts.error().message;
  ASSERT_EQ(layouts.value().size(), 1U);
  EXPECT_EQ(layouts.value().front().id, "arvane:chest");
}
