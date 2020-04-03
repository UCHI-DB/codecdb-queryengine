//
// Created by harper on 2/25/20.
//

#include <gtest/gtest.h>
#include "join.h"

using namespace lqf;

TEST(RowBuilderTest, Create) {
    RowBuilder rb({JL(0), JL(1), JR(0), JR(2)});
    EXPECT_EQ(4, rb.numFields());
    EXPECT_EQ(0, rb.numStringFields());
    EXPECT_EQ(false, rb.useVertical());

    MemDataRow left(4);
    MemDataRow right(3);

    left[0] = 424;
    left[1] = 3243;
    left[2] = 87452;
    left[3] = 21232323;
    right[0] = 33244;
    right[1] = 34359543;
    right[2] = 33901;

    auto lsn = rb.snapshot(left);
    auto rsn = rb.snapshot(right);
    EXPECT_EQ(424, (*lsn)[0].asInt());
    EXPECT_EQ(87452, (*lsn)[1].asInt());
    EXPECT_EQ(33244, (*rsn)[0].asInt());
    EXPECT_EQ(33901, (*rsn)[1].asInt());

    MemDataRow output(4);

    rb.build(output, left, right, 0);
    EXPECT_EQ(424, output[0].asInt());
    EXPECT_EQ(3243, output[1].asInt());
    EXPECT_EQ(33244, output[2].asInt());
    EXPECT_EQ(34359543, output[3].asInt());
}

TEST(RowBuilderTest, CreateWithString) {
    RowBuilder rb({JL(0), JL(1), JR(0), JR(2), JLS(3), JRS(3)});
    EXPECT_EQ(6, rb.numFields());
    EXPECT_EQ(2, rb.numStringFields());
    EXPECT_EQ(false, rb.useVertical());

    const vector<uint32_t> offset{0, 1, 2, 3, 5};
    const vector<uint32_t> offset2{0, 1, 2, 4};

    MemDataRow left(offset);
    MemDataRow right(offset2);

    ByteArray bytedata("ref");
    ByteArray bytedata2("ddb=");

    left[0] = 424;
    left[1] = 3243;
    left[2] = 87452;
    left[3] = bytedata;
    right[0] = 33244;
    right[1] = 34359543;
    right[2] = bytedata2;

    const vector<uint32_t> resoffset{0, 1, 2, 3, 4, 6, 8};
    MemDataRow output(resoffset);

    rb.build(output, left, right, 0);

    EXPECT_EQ(424, output[0].asInt());
    EXPECT_EQ(3243, output[1].asInt());
    EXPECT_EQ(33244, output[2].asInt());
    EXPECT_EQ(34359543, output[3].asInt());
    EXPECT_EQ(bytedata, output[4].asByteArray());
    EXPECT_EQ(bytedata2, output[5].asByteArray());
}

TEST(ColumnBuilderTest, Create) {
    ColumnBuilder cb({JL(0), JL(1), JR(2), JR(0)});
    EXPECT_EQ(true, cb.useVertical());
    EXPECT_EQ(0, cb.numStringFields());
    EXPECT_EQ(4, cb.numFields());

    EXPECT_EQ(vector<uint32_t>({1, 1}), cb.rightColSize());
    vector<pair<uint8_t, uint8_t>> vleft({pair<uint8_t, uint8_t>(0, 0), pair<uint8_t, uint8_t>({1, 1})});
    vector<pair<uint8_t, uint8_t>> vright({pair<uint8_t, uint8_t>(2, 2), pair<uint8_t, uint8_t>({0, 3})});
    EXPECT_EQ(vleft, cb.leftInst());
    EXPECT_EQ(vright, cb.rightInst());
}

TEST(ColumnBuilderTest, CreateWithString) {
    ColumnBuilder cb({JL(0), JL(1), JR(2), JR(0), JLS(3), JRS(3)});
    EXPECT_EQ(true, cb.useVertical());
    EXPECT_EQ(2, cb.numStringFields());
    EXPECT_EQ(6, cb.numFields());

    EXPECT_EQ(vector<uint32_t>({1, 1, 2}), cb.rightColSize());
    vector<pair<uint8_t, uint8_t>> vleft(
            {pair<uint8_t, uint8_t>(0, 0), pair<uint8_t, uint8_t>({1, 1}), pair<uint8_t, uint8_t>(3, 4)});
    vector<pair<uint8_t, uint8_t>> vright({pair<uint8_t, uint8_t>(2, 2), pair<uint8_t, uint8_t>({0, 3}),
                                           pair<uint8_t, uint8_t>(3, 5)});
    EXPECT_EQ(vleft, cb.leftInst());
    EXPECT_EQ(vright, cb.rightInst());
}

TEST(HashJoinTest, JoinWithoutKey) {

    auto left = ParquetTable::Open("lineitem");
    left->updateColumns((1 << 14) - 1);
    auto right = MemTable::Make(2);
    auto block = right->allocate(100);

    auto rows = block->rows();
    (*rows)[0][0] = 35; // 6
    (*rows)[1][0] = 99; // 4
    (*rows)[2][0] = 1154; // 6
    (*rows)[3][0] = 4452; // 2
    (*rows)[4][0] = 5987; // 4
    (*rows)[0][1] = 0;
    (*rows)[1][1] = 1;
    (*rows)[2][1] = 2;
    (*rows)[3][1] = 3;
    (*rows)[4][1] = 4;


    HashJoin join1(0, 0, new RowBuilder({JL(1), JL(2), JR(1), JLS(10)}));

    auto joined = join1.join(*left, *right);
    auto blocks = joined->blocks()->collect();
    auto rblock = (*blocks)[0];
    EXPECT_EQ(22, rblock->size());

    auto rrows = rblock->rows();
    int i = 0;

    {
        EXPECT_EQ(1, (*rrows)[i][0].asInt());
        EXPECT_EQ(4, (*rrows)[i][1].asInt());
        EXPECT_EQ(0, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1996-02-21"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(162, (*rrows)[i][0].asInt());
        EXPECT_EQ(1, (*rrows)[i][1].asInt());
        EXPECT_EQ(0, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1996-01-22"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(121, (*rrows)[i][0].asInt());
        EXPECT_EQ(4, (*rrows)[i][1].asInt());
        EXPECT_EQ(0, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1996-01-19"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(86, (*rrows)[i][0].asInt());
        EXPECT_EQ(7, (*rrows)[i][1].asInt());
        EXPECT_EQ(0, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1995-11-26"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(120, (*rrows)[i][0].asInt());
        EXPECT_EQ(7, (*rrows)[i][1].asInt());
        EXPECT_EQ(0, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1995-11-08"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(31, (*rrows)[i][0].asInt());
        EXPECT_EQ(7, (*rrows)[i][1].asInt());
        EXPECT_EQ(0, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1996-02-01"), (*rrows)[i][3].asByteArray());
        ++i;
    }

    {
        EXPECT_EQ(88, (*rrows)[i][0].asInt());
        EXPECT_EQ(9, (*rrows)[i][1].asInt());
        EXPECT_EQ(1, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1994-05-18"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(124, (*rrows)[i][0].asInt());
        EXPECT_EQ(5, (*rrows)[i][1].asInt());
        EXPECT_EQ(1, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1994-05-06"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(135, (*rrows)[i][0].asInt());
        EXPECT_EQ(1, (*rrows)[i][1].asInt());
        EXPECT_EQ(1, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1994-04-19"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(109, (*rrows)[i][0].asInt());
        EXPECT_EQ(2, (*rrows)[i][1].asInt());
        EXPECT_EQ(1, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1994-07-04"), (*rrows)[i][3].asByteArray());
        ++i;
    }

    {
        EXPECT_EQ(143, (*rrows)[i][0].asInt());
        EXPECT_EQ(10, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1992-04-17"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(148, (*rrows)[i][0].asInt());
        EXPECT_EQ(7, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1992-04-22"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(97, (*rrows)[i][0].asInt());
        EXPECT_EQ(1, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1992-06-07"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(1, (*rrows)[i][0].asInt());
        EXPECT_EQ(2, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1992-03-30"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(36, (*rrows)[i][0].asInt());
        EXPECT_EQ(2, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1992-02-26"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(196, (*rrows)[i][0].asInt());
        EXPECT_EQ(8, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1992-03-04"), (*rrows)[i][3].asByteArray());
        ++i;
    }

    {
        EXPECT_EQ(114, (*rrows)[i][0].asInt());
        EXPECT_EQ(8, (*rrows)[i][1].asInt());
        EXPECT_EQ(3, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1994-10-06"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(1, (*rrows)[i][0].asInt());
        EXPECT_EQ(8, (*rrows)[i][1].asInt());
        EXPECT_EQ(3, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1994-10-08"), (*rrows)[i][3].asByteArray());
        ++i;
    }

    {
        EXPECT_EQ(23, (*rrows)[i][0].asInt());
        EXPECT_EQ(2, (*rrows)[i][1].asInt());
        EXPECT_EQ(4, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1996-09-13"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(176, (*rrows)[i][0].asInt());
        EXPECT_EQ(5, (*rrows)[i][1].asInt());
        EXPECT_EQ(4, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1996-11-28"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(92, (*rrows)[i][0].asInt());
        EXPECT_EQ(3, (*rrows)[i][1].asInt());
        EXPECT_EQ(4, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1996-10-30"), (*rrows)[i][3].asByteArray());
        ++i;

        EXPECT_EQ(97, (*rrows)[i][0].asInt());
        EXPECT_EQ(1, (*rrows)[i][1].asInt());
        EXPECT_EQ(4, (*rrows)[i][2].asInt());
        EXPECT_EQ(ByteArray("1996-10-15"), (*rrows)[i][3].asByteArray());
    }

}

TEST(HashJoinTest, JoinWithKey) {
    HashJoin join(0, 0, new RowBuilder({JL(1), JL(2), JR(1), JLS(10)}, true));

    auto left = ParquetTable::Open("lineitem");
    left->updateColumns((1 << 14) - 1);
    auto right = MemTable::Make(2);
    auto block = right->allocate(100);

    auto rows = block->rows();
    (*rows)[0][0] = 35; // 6
    (*rows)[1][0] = 99; // 4
    (*rows)[2][0] = 1154; // 6
    (*rows)[3][0] = 4452; // 2
    (*rows)[4][0] = 5987; // 4
    (*rows)[0][1] = 0;
    (*rows)[1][1] = 1;
    (*rows)[2][1] = 2;
    (*rows)[3][1] = 3;
    (*rows)[4][1] = 4;


    auto joined = join.join(*left, *right);
    auto blocks = joined->blocks()->collect();
    auto rblock = (*blocks)[0];
    EXPECT_EQ(22, rblock->size());

    auto rrows = rblock->rows();
    int i = 0;

    {
        EXPECT_EQ(35, (*rrows)[i][0].asInt());
        EXPECT_EQ(1, (*rrows)[i][1].asInt());
        EXPECT_EQ(4, (*rrows)[i][2].asInt());
        EXPECT_EQ(0, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1996-02-21"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(35, (*rrows)[i][0].asInt());
        EXPECT_EQ(162, (*rrows)[i][1].asInt());
        EXPECT_EQ(1, (*rrows)[i][2].asInt());
        EXPECT_EQ(0, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1996-01-22"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(35, (*rrows)[i][0].asInt());
        EXPECT_EQ(121, (*rrows)[i][1].asInt());
        EXPECT_EQ(4, (*rrows)[i][2].asInt());
        EXPECT_EQ(0, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1996-01-19"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(35, (*rrows)[i][0].asInt());
        EXPECT_EQ(86, (*rrows)[i][1].asInt());
        EXPECT_EQ(7, (*rrows)[i][2].asInt());
        EXPECT_EQ(0, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1995-11-26"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(35, (*rrows)[i][0].asInt());
        EXPECT_EQ(120, (*rrows)[i][1].asInt());
        EXPECT_EQ(7, (*rrows)[i][2].asInt());
        EXPECT_EQ(0, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1995-11-08"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(35, (*rrows)[i][0].asInt());
        EXPECT_EQ(31, (*rrows)[i][1].asInt());
        EXPECT_EQ(7, (*rrows)[i][2].asInt());
        EXPECT_EQ(0, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1996-02-01"), (*rrows)[i][4].asByteArray());
        ++i;
    }

    {
        EXPECT_EQ(99, (*rrows)[i][0].asInt());
        EXPECT_EQ(88, (*rrows)[i][1].asInt());
        EXPECT_EQ(9, (*rrows)[i][2].asInt());
        EXPECT_EQ(1, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1994-05-18"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(99, (*rrows)[i][0].asInt());
        EXPECT_EQ(124, (*rrows)[i][1].asInt());
        EXPECT_EQ(5, (*rrows)[i][2].asInt());
        EXPECT_EQ(1, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1994-05-06"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(99, (*rrows)[i][0].asInt());
        EXPECT_EQ(135, (*rrows)[i][1].asInt());
        EXPECT_EQ(1, (*rrows)[i][2].asInt());
        EXPECT_EQ(1, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1994-04-19"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(99, (*rrows)[i][0].asInt());
        EXPECT_EQ(109, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(1, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1994-07-04"), (*rrows)[i][4].asByteArray());
        ++i;
    }

    {
        EXPECT_EQ(1154, (*rrows)[i][0].asInt());
        EXPECT_EQ(143, (*rrows)[i][1].asInt());
        EXPECT_EQ(10, (*rrows)[i][2].asInt());
        EXPECT_EQ(2, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1992-04-17"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(1154, (*rrows)[i][0].asInt());
        EXPECT_EQ(148, (*rrows)[i][1].asInt());
        EXPECT_EQ(7, (*rrows)[i][2].asInt());
        EXPECT_EQ(2, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1992-04-22"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(1154, (*rrows)[i][0].asInt());
        EXPECT_EQ(97, (*rrows)[i][1].asInt());
        EXPECT_EQ(1, (*rrows)[i][2].asInt());
        EXPECT_EQ(2, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1992-06-07"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(1154, (*rrows)[i][0].asInt());
        EXPECT_EQ(1, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(2, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1992-03-30"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(1154, (*rrows)[i][0].asInt());
        EXPECT_EQ(36, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(2, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1992-02-26"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(1154, (*rrows)[i][0].asInt());
        EXPECT_EQ(196, (*rrows)[i][1].asInt());
        EXPECT_EQ(8, (*rrows)[i][2].asInt());
        EXPECT_EQ(2, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1992-03-04"), (*rrows)[i][4].asByteArray());
        ++i;
    }

    {
        EXPECT_EQ(4452, (*rrows)[i][0].asInt());
        EXPECT_EQ(114, (*rrows)[i][1].asInt());
        EXPECT_EQ(8, (*rrows)[i][2].asInt());
        EXPECT_EQ(3, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1994-10-06"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(4452, (*rrows)[i][0].asInt());
        EXPECT_EQ(1, (*rrows)[i][1].asInt());
        EXPECT_EQ(8, (*rrows)[i][2].asInt());
        EXPECT_EQ(3, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1994-10-08"), (*rrows)[i][4].asByteArray());
        ++i;
    }

    {
        EXPECT_EQ(5987, (*rrows)[i][0].asInt());
        EXPECT_EQ(23, (*rrows)[i][1].asInt());
        EXPECT_EQ(2, (*rrows)[i][2].asInt());
        EXPECT_EQ(4, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1996-09-13"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(5987, (*rrows)[i][0].asInt());
        EXPECT_EQ(176, (*rrows)[i][1].asInt());
        EXPECT_EQ(5, (*rrows)[i][2].asInt());
        EXPECT_EQ(4, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1996-11-28"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(5987, (*rrows)[i][0].asInt());
        EXPECT_EQ(92, (*rrows)[i][1].asInt());
        EXPECT_EQ(3, (*rrows)[i][2].asInt());
        EXPECT_EQ(4, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1996-10-30"), (*rrows)[i][4].asByteArray());
        ++i;

        EXPECT_EQ(5987, (*rrows)[i][0].asInt());
        EXPECT_EQ(97, (*rrows)[i][1].asInt());
        EXPECT_EQ(1, (*rrows)[i][2].asInt());
        EXPECT_EQ(4, (*rrows)[i][3].asInt());
        EXPECT_EQ(ByteArray("1996-10-15"), (*rrows)[i][4].asByteArray());
    }

}

TEST(HashFilterJoinTest, Join) {
    auto left = ParquetTable::Open("lineitem");
    left->updateColumns((1 << 14) - 1);
    auto right = MemTable::Make(2);
    auto block = right->allocate(100);

    auto rows = block->rows();
    (*rows)[0][0] = 35; // 6
    (*rows)[1][0] = 99; // 4
    (*rows)[2][0] = 1154; // 6
    (*rows)[3][0] = 4452; // 2
    (*rows)[4][0] = 5987; // 4
    (*rows)[0][1] = 0;
    (*rows)[1][1] = 1;
    (*rows)[2][1] = 2;
    (*rows)[3][1] = 3;
    (*rows)[4][1] = 4;


    HashFilterJoin join(0, 0);

    auto joined = join.join(*left, *right);
    auto blocks = joined->blocks()->collect();
    auto rblock = (*blocks)[0];
    EXPECT_EQ(22, rblock->size());
}

TEST(HashExistJoinTest, Join) {
    auto left = ParquetTable::Open("lineitem");
    left->updateColumns((1 << 14) - 1);
    auto right = MemTable::Make(2);
    auto block = right->allocate(100);

    auto rows = block->rows();
    (*rows)[0][0] = 35; // 6
    (*rows)[1][0] = 99; // 4
    (*rows)[2][0] = 1154; // 6
    (*rows)[3][0] = 4452; // 2
    (*rows)[4][0] = 5987; // 4
    (*rows)[5][0] = 14145;
    (*rows)[6][0] = 21859;
    (*rows)[7][0] = 40;
    (*rows)[0][1] = 0;
    (*rows)[1][1] = 1;
    (*rows)[2][1] = 2;
    (*rows)[3][1] = 3;
    (*rows)[4][1] = 4;
    (*rows)[5][1] = 5;
    (*rows)[6][1] = 6;
    (*rows)[7][1] = 7;


    HashExistJoin join(0, 0, new RowBuilder({JR(0), JR(1)}));

    auto joined = join.join(*left, *right);
    EXPECT_EQ(2, joined->numFields());

    auto blocks = joined->blocks()->collect();
    auto rblock = (*blocks)[0];
    EXPECT_EQ(5, rblock->size());

    auto read_rows = rblock->rows();

    int i = 0;
    EXPECT_EQ(35, (*read_rows)[i][0].asInt());
    EXPECT_EQ(0, (*read_rows)[i][1].asInt());
    ++i;
    EXPECT_EQ(99, (*read_rows)[i][0].asInt());
    EXPECT_EQ(1, (*read_rows)[i][1].asInt());
    ++i;
    EXPECT_EQ(1154, (*read_rows)[i][0].asInt());
    EXPECT_EQ(2, (*read_rows)[i][1].asInt());
    ++i;
    EXPECT_EQ(4452, (*read_rows)[i][0].asInt());
    EXPECT_EQ(3, (*read_rows)[i][1].asInt());
    ++i;
    EXPECT_EQ(5987, (*read_rows)[i][0].asInt());
    EXPECT_EQ(4, (*read_rows)[i][1].asInt());
}

TEST(HashColumnJoinTest, Join) {
    auto left = MemTable::Make(2, true);
    auto lblock1 = left->allocate(100);
    auto lblock2 = left->allocate(150);

    std::random_device rd;  //Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
    std::uniform_real_distribution<> dis(1.0, 2.0);

    auto lrow1 = lblock1->rows();
    for (int i = 0; i < 100; i++) {
        int32_t rand = gen() % 10;
        (*lrow1)[i][0] = rand;
        (*lrow1)[i][1] = dis(gen);
    }
    auto lrow2 = lblock2->rows();
    for (int i = 0; i < 150; i++) {
        int32_t rand = gen() % 10;
        (*lrow2)[i][0] = rand;
        (*lrow2)[i][1] = dis(gen);
    }

    auto right = MemTable::Make(2);
    auto block = right->allocate(10);
    auto rows = block->rows();

    array<int32_t, 10> data{35, 99, 1154, 4452, 5987, 14145, 21859, 40, 1230, 3234};

    (*rows)[0][0] = 35;
    (*rows)[1][0] = 99;
    (*rows)[2][0] = 1154;
    (*rows)[3][0] = 4452;
    (*rows)[4][0] = 5987;
    (*rows)[5][0] = 14145;
    (*rows)[6][0] = 21859;
    (*rows)[7][0] = 40;
    (*rows)[8][0] = 1230;
    (*rows)[9][0] = 3234;
    (*rows)[0][1] = 0;
    (*rows)[1][1] = 1;
    (*rows)[2][1] = 2;
    (*rows)[3][1] = 3;
    (*rows)[4][1] = 4;
    (*rows)[5][1] = 5;
    (*rows)[6][1] = 6;
    (*rows)[7][1] = 7;
    (*rows)[8][1] = 8;
    (*rows)[9][1] = 9;

    HashColumnJoin join(0, 1, new ColumnBuilder({JL(0), JL(1), JR(0)}));

    auto joined = join.join(*left, *right);
    EXPECT_EQ(3, joined->numFields());

    auto results = joined->blocks()->collect();
    EXPECT_EQ(results->size(), 2);

    auto block1 = (*results)[0];
    auto asmvblock = static_pointer_cast<MemvBlock>(block1);
    EXPECT_TRUE(asmvblock.get() != nullptr);

    auto block2 = (*results)[1];
    asmvblock = static_pointer_cast<MemvBlock>(block2);
    EXPECT_TRUE(asmvblock.get() != nullptr);

    EXPECT_EQ(100, block1->size());
    auto res_rows = block1->rows();
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(data[(*res_rows)[i][0].asInt()], (*res_rows)[i][2].asInt());
    }

    EXPECT_EQ(150, block2->size());
    res_rows = block2->rows();
    for (int i = 0; i < 150; ++i) {
        EXPECT_EQ(data[(*res_rows)[i][0].asInt()], (*res_rows)[i][2].asInt());
    }
}