//
// Created by Harper on 1/3/21.
//

#include "query3.h"

namespace lqf {
    namespace ssb {
        namespace q3_3 {
            ByteArray city1("UNITED KI1");
            ByteArray city2("UNITED KI5");
            ByteArray date_from("19920101");
            ByteArray date_to("19971231");
        }

        using namespace q3;
        using namespace q3_3;
        using namespace sboost;


        void executeQ3_3Plain() {
            auto customerTable = ParquetTable::Open(Customer::path,
                                                    {Customer::CITY, Customer::CUSTKEY});
            auto lineorderTable = ParquetTable::Open(LineOrder::path,
                                                     {LineOrder::CUSTKEY, LineOrder::SUPPKEY, LineOrder::ORDERDATE,
                                                      LineOrder::REVENUE});
            auto supplierTable = ParquetTable::Open(Supplier::path,
                                                    {Supplier::SUPPKEY, Supplier::CITY});

            function<bool(const ByteArray &)> match = [=](const ByteArray &data) {
                return data == city1 || data == city2;
            };

            ColFilter supplierFilter(
                    new SBoostByteArrayPredicate(Supplier::CITY, bind(ByteArrayDictMultiEq::build, match)));
            auto filteredSupplier = supplierFilter.filter(*supplierTable);

            ColFilter custFilter(
                    new SBoostByteArrayPredicate(Customer::CITY, bind(ByteArrayDictMultiEq::build, match)));
            auto filteredCustomer = custFilter.filter(*customerTable);

            ColFilter orderFilter(
                    new SBoostByteArrayPredicate(LineOrder::ORDERDATE,
                                                 bind(ByteArrayDictBetween::build, date_from, date_to)));
            auto filteredOrder = orderFilter.filter(*lineorderTable);

            HashJoin orderSupplierJoin(LineOrder::SUPPKEY, Supplier::SUPPKEY, new WithCityBuilder());
            // CUSTKEY, S_CITY, YEAR, REVENUE
            auto orderWithSupp = orderSupplierJoin.join(*filteredOrder, *filteredSupplier);

            HashColumnJoin allJoin(0, Customer::CUSTKEY,
                                   new ColumnBuilder({JRR(Customer::CITY), JL(1), JL(2), JL(3)}), true);
            auto allJoined = allJoin.join(*orderWithSupp, *filteredCustomer);

            function<uint64_t(DataRow &)> hasher = [](DataRow &data) {
                return (data[0].asInt() << 22) + (data[1].asInt() << 12) + data[2].asInt();
            };
            function<vector<AggField *>()> aggFields = []() {
                return vector<AggField *>{new DoubleSum(3)};
            };
            HashAgg agg(hasher, RowCopyFactory().field(F_REGULAR, 0, 0)
                    ->field(F_REGULAR, 1, 1)
                    ->field(F_REGULAR, 2, 2)->buildSnapshot(), aggFields);
            auto agged = agg.agg(*allJoined);

            function<bool(DataRow *, DataRow *)> comparator = [](DataRow *a, DataRow *b) {
                return SILE(2) || (SIE(2) && SDGE(3));
            };
            SmallSort sort(comparator);
            auto sorted = sort.sort(*agged);

            // TODO use dictionary to print column 1
            Printer printer(PBEGIN PI(0) PI(1) PI(2) PD(3) PEND);
            printer.print(*sorted);;
        }

        void executeQ3_3() {
            ExecutionGraph graph;

            auto customer = ParquetTable::Open(Customer::path,
                                               {Customer::CITY, Customer::CUSTKEY});
            auto lineorder = ParquetTable::Open(LineOrder::path,
                                                {LineOrder::CUSTKEY, LineOrder::SUPPKEY, LineOrder::ORDERDATE,
                                                 LineOrder::REVENUE});
            auto supplier = ParquetTable::Open(Supplier::path,
                                               {Supplier::SUPPKEY, Supplier::CITY});

            auto customerTable = graph.add(new TableNode(customer), {});
            auto lineorderTable = graph.add(new TableNode(lineorder), {});
            auto supplierTable = graph.add(new TableNode(supplier), {});

            function<bool(const ByteArray &)> match = [=](const ByteArray &data) {
                return data == city1 || data == city2;
            };

            auto supplierFilter = graph.add(new ColFilter(
                    new SBoostByteArrayPredicate(Supplier::CITY, bind(ByteArrayDictMultiEq::build, match))),
                                            {supplierTable});
            auto custFilter = graph.add(new ColFilter(
                    new SBoostByteArrayPredicate(Customer::CITY, bind(ByteArrayDictMultiEq::build, match))),
                                        {customerTable});
            auto orderFilter = graph.add(new ColFilter(
                    new SBoostByteArrayPredicate(LineOrder::ORDERDATE,
                                                 bind(ByteArrayDictBetween::build, date_from, date_to))),
                                         {lineorderTable});

            auto orderSupplierJoin = graph.add(
                    new HashTJoin<Hash32SparseContainer>(LineOrder::SUPPKEY, Supplier::SUPPKEY, new WithCityBuilder()),
                    {orderFilter, supplierFilter});
            // CUSTKEY, S_NATION, YEAR, REVENUE

            auto allJoin = graph.add(new HashColumnTJoin<Hash32SparseContainer>(0, Customer::CUSTKEY, new ColumnBuilder(
                    {JRR(Customer::CITY), JL(1), JL(2), JL(3)}), true),
                                     {orderSupplierJoin, custFilter});

            function<uint64_t(DataRow &)> hasher = [](DataRow &data) {
                return (data[0].asInt() << 22) + (data[1].asInt() << 12) + data[2].asInt();
            };
            function<vector<AggField *>()> aggFields = []() {
                return vector<AggField *>{new DoubleSum(3)};
            };
            auto agg = graph.add(new HashAgg(hasher, RowCopyFactory().field(F_REGULAR, 0, 0)
                    ->field(F_REGULAR, 1, 1)
                    ->field(F_REGULAR, 2, 2)->buildSnapshot(), aggFields), {allJoin});

            function<bool(DataRow *, DataRow *)> comparator = [](DataRow *a, DataRow *b) {
                return SILE(2) || (SIE(2) && SDGE(3));
            };
            auto sort = graph.add(new SmallSort(comparator), {agg});

            // TODO use dictionary to print column 1
            graph.add(new Printer(PBEGIN PI(0) PI(1) PI(2) PD(3) PEND), {sort});

            graph.execute(true);
        }
    }
}