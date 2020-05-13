//
// Created by harper on 4/6/20.
//

#include <parquet/types.h>
#include <lqf/data_model.h>
#include <lqf/filter.h>
#include <lqf/agg.h>
#include <lqf/sort.h>
#include <lqf/print.h>
#include <lqf/join.h>
#include <lqf/mat.h>
#include "tpchquery.h"


namespace lqf {
    namespace tpch {

        namespace q11 {

            ByteArray nationChosen("GERMANY");
            double fraction = 0.0001;

            class CostField : public agg::DoubleSum {
            public:
                CostField() : agg::DoubleSum(0) {}

                void reduce(DataRow &row) {
                    *value_ += row[PartSupp::AVAILQTY].asInt() * row[PartSupp::SUPPLYCOST].asDouble();
                }
            };

            class TotalAggNode : public SimpleAgg {
            public:
                TotalAggNode(const vector<uint32_t> &col_size, function<vector<AggField *>()> agg_fields) : SimpleAgg(
                        col_size, agg_fields) {}

                unique_ptr<NodeOutput> execute(const vector<NodeOutput *> &input) override {
                    auto input0 = static_cast<TableOutput *>(input[0]);
                    auto result = agg(*(input0->get()));
                    double total_value = (*(*result->blocks()->collect())[0]->rows())[0][0].asDouble();
                    double threshold = total_value * fraction;
                    return unique_ptr<TypedOutput<double>>(new TypedOutput(threshold));
                }
            };

            class PartAggNode : public NestedNode {
            public:
                PartAggNode(Node *inner) : NestedNode(inner, 2) {}

                unique_ptr<NodeOutput> execute(const vector<NodeOutput *> &input) override {
                    auto inneragg = dynamic_cast<HashAgg *>(inner_.get());
                    auto total = (static_cast<TypedOutput<double> *>(input[0]))->get();
                    inneragg->setPredicate([=](DataRow &input) {
                        return input[1].asDouble() >= total;
                    });
                    auto input0 = static_cast<TableOutput *>(input[1]);
                    auto result = inneragg->agg(*(input0->get()));
                    return unique_ptr<TableOutput>(new TableOutput(result));
                }
            };
        }
        using namespace q11;

        void executeQ11() {

            ExecutionGraph graph;

            auto nation = graph.add(new TableNode(ParquetTable::Open(Nation::path, {Nation::NATIONKEY, Nation::NAME})),
                                    {});
            auto supplier = graph.add(
                    new TableNode(ParquetTable::Open(Supplier::path, {Supplier::NATIONKEY, Supplier::SUPPKEY})), {});
            auto partsupp = graph.add(new TableNode(ParquetTable::Open(PartSupp::path,
                                                                       {PartSupp::SUPPKEY, PartSupp::PARTKEY,
                                                                        PartSupp::AVAILQTY,
                                                                        PartSupp::SUPPLYCOST})), {});
            auto nationNameFilter = graph.add(
                    new ColFilter({new SimplePredicate(Nation::NAME, [=](const DataField &field) {
                        return field.asByteArray() == nationChosen;
                    })}), {nation});

            auto validSupplierJoin = graph.add(new HashFilterJoin(Supplier::NATIONKEY, Nation::NATIONKEY),
                                               {supplier, nationNameFilter});

            auto validPsJoin = graph.add(new HashFilterJoin(PartSupp::SUPPKEY, Supplier::SUPPKEY),
                                         {partsupp, validSupplierJoin});

            auto matPs = graph.add(new FilterMat(), {validPsJoin});

            function<vector<AggField *>()> agg_fields = []() { return vector<AggField *>{new CostField()}; };
            auto totalAgg = graph.add(new TotalAggNode(vector<uint32_t>({1}), agg_fields), {matPs});


            auto bypartAgg = graph.add(
                    new PartAggNode(new HashAgg(vector<uint32_t>({1, 1}), {AGI(PartSupp::PARTKEY)}, agg_fields,
                                                COL_HASHER(PartSupp::PARTKEY))), {totalAgg, matPs});

            function<bool(DataRow *, DataRow *)> comparator = [](DataRow *a, DataRow *b) { return SDGE(1); };
            auto sort = graph.add(new SmallSort(comparator), {bypartAgg});

            graph.add(new Printer(PBEGIN PI(0) PD(1) PEND), {sort});

            graph.execute();
        }

        void executeQ11Backup() {

            auto nation = ParquetTable::Open(Nation::path, {Nation::NATIONKEY, Nation::NAME});
            auto supplier = ParquetTable::Open(Supplier::path, {Supplier::NATIONKEY, Supplier::SUPPKEY});
            auto partsupp = ParquetTable::Open(PartSupp::path,
                                               {PartSupp::SUPPKEY, PartSupp::PARTKEY, PartSupp::AVAILQTY,
                                                PartSupp::SUPPLYCOST});

            ColFilter nationNameFilter({new SimplePredicate(Nation::NAME, [=](const DataField &field) {
                return field.asByteArray() == nationChosen;
            })});
            auto validNation = nationNameFilter.filter(*nation);

            HashFilterJoin validSupplierJoin(Supplier::NATIONKEY, Nation::NATIONKEY);
            auto validSupplier = validSupplierJoin.join(*supplier, *validNation);

            HashFilterJoin validPsJoin(PartSupp::SUPPKEY, Supplier::SUPPKEY);
            auto validps = FilterMat().mat(*validPsJoin.join(*partsupp, *validSupplier));

            function<vector<AggField *>()> agg_fields = []() { return vector<AggField *>{new CostField()}; };
            SimpleAgg totalAgg(vector<uint32_t>({1}), agg_fields);
            auto total = totalAgg.agg(*validps);
            double total_value = (*(*total->blocks()->collect())[0]->rows())[0][0].asDouble();
            double threshold = total_value * fraction;

            HashAgg bypartAgg(vector<uint32_t>({1, 1}), {AGI(PartSupp::PARTKEY)}, agg_fields,
                              COL_HASHER(PartSupp::PARTKEY));
            bypartAgg.setPredicate([=](DataRow &input) {
                return input[1].asDouble() >= threshold;
            });
            auto byParts = bypartAgg.agg(*validps);

            function<bool(DataRow *, DataRow *)> comparator = [](DataRow *a, DataRow *b) { return SDGE(1); };
            SmallSort sort(comparator);
            auto sorted = sort.sort(*byParts);

            Printer printer(PBEGIN PI(0) PD(1) PEND);
            printer.print(*sorted);
        }
    }
}