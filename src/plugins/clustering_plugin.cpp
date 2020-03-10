#include "clustering_plugin.hpp"

#include "operators/get_table.hpp"
#include "operators/sort.hpp"
#include "operators/table_wrapper.hpp"
#include "operators/update.hpp"
#include "operators/validate.hpp"
#include "storage/base_segment.hpp"
#include "storage/pos_list.hpp"
#include "storage/reference_segment.hpp"
#include "storage/segment_encoding_utils.hpp"
#include "storage/table.hpp"
#include "statistics/table_statistics.hpp"
#include "statistics/generate_pruning_statistics.hpp"
#include "resolve_type.hpp"

namespace opossum {

const std::string ClusteringPlugin::description() const { return "ClusteringPlugin"; }

void ClusteringPlugin::start() {
  Hyrise::get().log_manager.add_message(description(), "Initialized!");
  _loop_thread = std::make_unique<PausableLoopThread>(THREAD_INTERVAL, [&](size_t) { _optimize_clustering(); });
}

void ClusteringPlugin::_optimize_clustering() {
  if (_optimized) return;

  _optimized = true;

  std::map<std::string, std::string> sort_orders = {{"orders", "o_orderdate"}, {"lineitem", "l_shipdate"}};

  for (auto& [table_name, column_name] : sort_orders) {
    if (!Hyrise::get().storage_manager.has_table(table_name)) {
      Hyrise::get().log_manager.add_message(description(), "No optimization possible with given parameters for " + table_name + " table!");
      return;
    }
    auto table = Hyrise::get().storage_manager.get_table(table_name);

    const auto sort_column_id = table->column_id_by_name(column_name);

    auto table_wrapper = std::make_shared<TableWrapper>(table);
    table_wrapper->execute();
    auto sort = std::make_shared<Sort>(table_wrapper, sort_column_id, OrderByMode::Ascending, Chunk::DEFAULT_SIZE);
    sort->execute();
    const auto immutable_sorted_table = sort->get_output();

    Assert(immutable_sorted_table->chunk_count() == table->chunk_count(), "Mismatching chunk_count");

    table = std::make_shared<Table>(immutable_sorted_table->column_definitions(), TableType::Data,
                                    table->target_chunk_size(), UseMvcc::Yes);
    const auto column_count = immutable_sorted_table->column_count();
    const auto chunk_count = immutable_sorted_table->chunk_count();
    for (auto chunk_id = ChunkID{0}; chunk_id < chunk_count; ++chunk_id) {
      const auto chunk = immutable_sorted_table->get_chunk(chunk_id);
      auto mvcc_data = std::make_shared<MvccData>(chunk->size(), CommitID{0});
      Segments segments{};
      for (auto column_id = ColumnID{0}; column_id < column_count; ++column_id) {
        segments.emplace_back(chunk->get_segment(column_id));
      }
      table->append_chunk(segments, mvcc_data);
      table->get_chunk(chunk_id)->set_ordered_by({sort_column_id,  OrderByMode::Ascending});
      table->get_chunk(chunk_id)->finalize();
    }

    table->set_table_statistics(TableStatistics::from_table(*table));
    generate_chunk_pruning_statistics(table);

    Hyrise::get().storage_manager.replace_table(table_name, table);
    if (Hyrise::get().default_lqp_cache)
      Hyrise::get().default_lqp_cache->clear();
    if (Hyrise::get().default_pqp_cache)
      Hyrise::get().default_pqp_cache->clear();

    Hyrise::get().log_manager.add_message(description(), "Applied new clustering configuration (" + column_name + ") to " + table_name + " table.");
  }

  // if (!Hyrise::get().storage_manager.has_table("lineitem")) {
  //   Hyrise::get().log_manager.add_message(description(), "No optimization possible with given parameters!");
  //   return;
  // }
  // auto table = Hyrise::get().storage_manager.get_table("lineitem");

  // const auto sort_column_id = ColumnID{10}; //l_shipdate
  // if (table->column_count() <= static_cast<ColumnCount>(sort_column_id)) {
  //   Hyrise::get().log_manager.add_message(description(), "No optimization possible with given parameters!");
  //   return;
  // }

  // auto table_wrapper = std::make_shared<TableWrapper>(table);
  // table_wrapper->execute();
  // auto sort = std::make_shared<Sort>(table_wrapper, sort_column_id, OrderByMode::Ascending, Chunk::DEFAULT_SIZE);
  // sort->execute();
  // const auto immutable_sorted_table = sort->get_output();

  // Assert(immutable_sorted_table->chunk_count() == table->chunk_count(), "Mismatching chunk_count");

  // table = std::make_shared<Table>(immutable_sorted_table->column_definitions(), TableType::Data,
  //                                 table->target_chunk_size(), UseMvcc::Yes);
  // const auto column_count = immutable_sorted_table->column_count();
  // const auto chunk_count = immutable_sorted_table->chunk_count();
  // for (auto chunk_id = ChunkID{0}; chunk_id < chunk_count; ++chunk_id) {
  //   const auto chunk = immutable_sorted_table->get_chunk(chunk_id);
  //   auto mvcc_data = std::make_shared<MvccData>(chunk->size(), CommitID{0});
  //   Segments segments{};
  //   for (auto column_id = ColumnID{0}; column_id < column_count; ++column_id) {
  //     segments.emplace_back(chunk->get_segment(column_id));
  //   }
  //   table->append_chunk(segments, mvcc_data);
  //   table->get_chunk(chunk_id)->set_ordered_by({sort_column_id,  OrderByMode::Ascending});
  // }

  // table->set_table_statistics(TableStatistics::from_table(*table));
  // generate_chunk_pruning_statistics(table);

  // Hyrise::get().storage_manager.replace_table("lineitem", table);
  // Hyrise::get().log_manager.add_message(description(), "Applied new clustering configuration to lineitem table.");
}

void ClusteringPlugin::stop() {}


EXPORT_PLUGIN(ClusteringPlugin)

}  // namespace opossum