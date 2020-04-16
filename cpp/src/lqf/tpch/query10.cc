//
// Created by harper on 4/4/20.
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

        using namespace agg;
        using namespace sboost;
        namespace q10 {
            ByteArray dateFrom("1993-10-01");
            ByteArray dateTo("1994-02-01");
            ByteArray returnflag("R");

            class RevBuilder : public RowBuilder {
            public:
                RevBuilder() : RowBuilder({JR(Orders::CUSTKEY), JL(0)}, false, true) {}

                void build(DataRow &target, DataRow &left, DataRow &right, int key) override {
                    target[0] = right[0].asInt();
                    target[1] = left[LineItem::EXTENDEDPRICE].asDouble() * (1 - left[LineItem::DISCOUNT].asDouble());
                }
            };
        }

        using namespace q10;

        void executeQ10() {
            auto customer = ParquetTable::Open(Customer::path,
                                               {Customer::CUSTKEY, Customer::NATIONKEY, Customer::ACCTBAL,
                                                Customer::NAME, Customer::ADDRESS, Customer::PHONE, Customer::COMMENT});
            auto order = ParquetTable::Open(Orders::path, {Orders::ORDERDATE, Orders::ORDERKEY, Orders::CUSTKEY});
            auto lineitem = ParquetTable::Open(LineItem::path,
                                               {LineItem::ORDERKEY, LineItem::RETURNFLAG, LineItem::EXTENDEDPRICE,
                                                LineItem::DISCOUNT});
            auto nation = ParquetTable::Open(Nation::path, {Nation::NAME, Nation::NATIONKEY});

            ColFilter orderDateFilter({new SboostPredicate<ByteArrayType>(Orders::ORDERDATE,
                                                                          bind(&ByteArrayDictRangele::build, dateFrom,
                                                                               dateTo))});
            auto validOrder = orderDateFilter.filter(*order);

            ColFilter lineitemFilter({new SboostPredicate<ByteArrayType>(LineItem::RETURNFLAG,
                                                                         bind(&ByteArrayDictEq::build, returnflag))});
            auto validLineitem = lineitemFilter.filter(*lineitem);

            HashJoin orderItemFilter(LineItem::ORDERKEY, Orders::ORDERKEY, new RevBuilder());
            // CUSTKEY, REV
            validLineitem = orderItemFilter.join(*validLineitem, *validOrder);

            HashAgg itemAgg(vector<uint32_t>({1, 1}), {AGI(0)},
                            []() { return vector<AggField *>({new DoubleSum(1)}); }, COL_HASHER(0));
            auto agglineitem = itemAgg.agg(*validLineitem);

            function<bool(DataRow *, DataRow *)> comparator = [](DataRow *a, DataRow *b) {
                return SDGE(1);
            };
            TopN top(20, comparator);
            // CUSTKEY, REV
            auto sorted = top.sort(*agglineitem);

            HashJoin customerJoin(Customer::CUSTKEY, 0, new RowBuilder(
                    {JR(1), JL(Customer::NATIONKEY), JLS(Customer::NAME), JL(Customer::ACCTBAL), JLS(Customer::ADDRESS),
                     JLS(Customer::PHONE), JLS(Customer::COMMENT)}, false ));
            // REV NATIONKEY NAME ACCTBAL ADDR PHONE CMT
            auto result = customerJoin.join(*customer, *sorted);

            HashJoin nationJoin(1, Nation::NATIONKEY, new RowBuilder(
                    {JL(0), JLS(2), JL(3), JLS(4), JLS(5), JLS(6), JRS(Nation::NAME)}));
            // REV NAME ACCTBAL ADDR PHONE CMT NATION_NAME
            auto custWithNation = nationJoin.join(*result, *nation);

            function<bool(DataRow *, DataRow *)> comparator2 = [](DataRow *a, DataRow *b) {
                return SDGE(0);
            };
            SmallSort lastsort(comparator);
            auto lastsorted = lastsort.sort(*custWithNation);

            Printer printer(PBEGIN PD(0) PB(1) PD(2) PB(3) PB(4) PB(5) PB(6) PEND);
            printer.print(*lastsorted);
        }
    }
}