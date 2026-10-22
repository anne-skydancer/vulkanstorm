# -*- cmake -*-
# Inventory rung (vkstorm-rust rung seven), recorder R2: the inventory model
# and its cache. Owned by R2 alone. Included from the LL_TESTS block of
# indra/newview/CMakeLists.txt; it will register INTEGRATION_TEST_llinventorymodel_parity.
# R2 also records the inventory UDP message handlers (UpdateCreateInventoryItem,
# BulkUpdateInventory in both forms, Remove*, MoveInventoryItem,
# SaveAssetIntoInventory) through the real model.
