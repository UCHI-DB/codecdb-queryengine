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
        namespace q6 {
            int quantity = 24;
            ByteArray dateFrom("1994-01-01");
            ByteArray dateTo("1995-01-01");
            double discountFrom = 0.04;
            double discountTo = 0.06;

            class PriceField : public agg::Sum<double, agg::AsDouble> {
            public:
                PriceField() : agg::DoubleSum(0) {}

                void reduce(DataRow &input) {
                    *value_ += input[LineItem::EXTENDEDPRICE].asDouble() * input[LineItem::DISCOUNT].asDouble();
                }
            };
        }
        using namespace q6;

        void executeQ6() {

            auto lineitemTable = ParquetTable::Open(LineItem::path);


            using namespace sboost;
            ColFilter filter({new SboostPredicate<ByteArrayType>(LineItem::SHIPDATE,
                                                                 bind(&ByteArrayDictRangele::build, dateFrom,
                                                                      dateTo)),
                              new SboostPredicate<DoubleType>(LineItem::DISCOUNT,
                                                              bind(&DoubleDictBetween::build, discountFrom,
                                                                   discountTo)),
                              new SboostPredicate<Int32Type>(LineItem::QUANTITY,
                                                             bind(&Int32DictLess::build, quantity))
                             });
            auto filtered = filter.filter(*lineitemTable);


            SimpleAgg agg(vector<uint32_t>({1}), []() { return vector<AggField *>({new PriceField()}); });
            auto agged = agg.agg(*filtered);

            auto printer = Printer::Make(PBEGIN PD(0) PEND);
            printer->print(*agged);
        }
    }
}