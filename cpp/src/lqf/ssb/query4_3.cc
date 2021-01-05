//
// Created by Harper on 1/3/21.
//

#include "query4.h"

namespace lqf {
    namespace ssb {
        namespace q4_3 {
            class OrderProfitBuilder : public RowBuilder {
            public:
                OrderProfitBuilder() : RowBuilder(
                        {JL(LineOrder::ORDERDATE), JL(LineOrder::REVENUE), JL(LineOrder::SUPPLYCOST),
                         JL(LineOrder::PARTKEY), JRR(Supplier::CITY)}, false, false) {}

                void build(DataRow &target, DataRow &left, DataRow &right, int32_t key) override {
                    target[0] = udf::date2year(left[LineOrder::ORDERDATE].asByteArray());
                    target[1] = right(Supplier::CITY).asInt();
                    target[2] = left[LineOrder::PARTKEY].asInt();
                    target[3] = left[LineOrder::REVENUE].asDouble() - left[LineOrder::SUPPLYCOST].asDouble();
                }
            };

            ByteArray region("AMERICA");
            ByteArray nation("UNITED STATES");
            ByteArray category("MFGR#14");
            ByteArray year_from("19970101");
            ByteArray year_to("19981231");
        }

        using namespace q4;
        using namespace q4_3;
        using namespace sboost;

        void executeQ4_3() {
            auto customerTable = ParquetTable::Open(Customer::path, {Customer::CUSTKEY, Customer::NATION});
            auto partTable = ParquetTable::Open(Part::path, {Part::CATEGORY, Part::BRAND, Part::PARTKEY});
            auto supplierTable = ParquetTable::Open(Supplier::path, {Supplier::SUPPKEY, Supplier::REGION});
            auto lineorderTable = ParquetTable::Open(LineOrder::path,
                                                     {LineOrder::ORDERDATE, LineOrder::SUPPKEY, LineOrder::PARTKEY,
                                                      LineOrder::CUSTKEY, LineOrder::REVENUE, LineOrder::SUPPLYCOST});

            ColFilter suppFilter(new SBoostByteArrayPredicate(Supplier::NATION, bind(ByteArrayDictEq::build, nation)));
            auto filteredSupp = suppFilter.filter(*supplierTable);

            ColFilter custFilter(new SBoostByteArrayPredicate(Customer::REGION, bind(ByteArrayDictEq::build, region)));
            auto filteredCustomer = custFilter.filter(*customerTable);

            ColFilter partFilter(new SBoostByteArrayPredicate(Part::CATEGORY, bind(ByteArrayDictEq::build, category)));
            auto filteredPart = partFilter.filter(*partTable);

            ColFilter orderFilter(new SBoostByteArrayPredicate(LineOrder::ORDERDATE,
                                                               bind(ByteArrayDictBetween::build, year_from, year_to)));
            auto filteredOrder = orderFilter.filter(*lineorderTable);

            FilterJoin custFilterJoin(LineOrder::CUSTKEY, Customer::CUSTKEY);
            auto orderOnValidCust = custFilterJoin.join(*filteredOrder, *filteredCustomer);

            HashJoin withSuppJoin(LineOrder::SUPPKEY, Supplier::SUPPKEY, new OrderProfitBuilder());
            auto orderWithSupp = withSuppJoin.join(*orderOnValidCust, *filteredSupp);

            HashJoin withPartJoin(2, Part::PARTKEY,
                                  new RowBuilder({JL(0), JL(1), JRR(Part::BRAND), JL(3)}, false, false));
            auto validOrder = withPartJoin.join(*orderWithSupp, *filteredPart);

            function<uint64_t(DataRow &)> hasher = [](DataRow &data) {
                return (data[0].asInt() << 20) + (data[1].asInt() << 10) + data[2].asInt();
            };
            function<vector<AggField *>()> aggFields = []() {
                return vector<AggField *>{new DoubleSum(3)};
            };
            HashAgg agg(hasher, RowCopyFactory().field(F_REGULAR, 0, 0)
                    ->field(F_REGULAR, 1, 1)->field(F_REGULAR, 2, 2)->buildSnapshot(), aggFields);
            auto agged = agg.agg(*validOrder);

            function<bool(DataRow *, DataRow *)> comparator = [](DataRow *a, DataRow *b) {
                return SILE(0) || (SIE(0) && SILE(1)) || (SIE(0) && SIE(1) && SILE(2));
            };
            SmallSort sort(comparator);
            auto sorted = sort.sort(*agged);

            // TODO use dictionary to print column 1
            Printer printer(PBEGIN PI(0) PI(1) PI(2) PD(3) PEND);
            printer.print(*sorted);;
        }
    }
}
