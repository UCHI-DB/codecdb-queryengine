//
// Created by harper on 2/9/20.
//

#include <iostream>
#include <exception>
#include <arrow/util/bit_stream_utils.h>
#include <parquet/encoding.h>
#include <parquet/column_reader.h>
#include "validate.h"
#include "data_model.h"

using namespace std;

namespace lqf {

    mt19937 Block::rand_ = mt19937(time(NULL));

    const array<vector<uint32_t>, 10> OFFSETS = {
            vector<uint32_t>({0}),
            {0, 1},
            {0, 1, 2},
            {0, 1, 2, 3},
            {0, 1, 2, 3, 4},
            {0, 1, 2, 3, 4, 5},
            {0, 1, 2, 3, 4, 5, 6},
            {0, 1, 2, 3, 4, 5, 6, 7},
            {0, 1, 2, 3, 4, 5, 6, 7, 8},
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}
    };

    const array<vector<uint32_t>, 10> SIZES = {
            vector<uint32_t>({1}),
            vector<uint32_t>({1}),
            vector<uint32_t>({1, 1}),
            vector<uint32_t>({1, 1, 1}),
            vector<uint32_t>({1, 1, 1, 1}),
            vector<uint32_t>({1, 1, 1, 1, 1}),
            vector<uint32_t>({1, 1, 1, 1, 1, 1}),
            vector<uint32_t>({1, 1, 1, 1, 1, 1, 1}),
            vector<uint32_t>({1, 1, 1, 1, 1, 1, 1, 1}),
            vector<uint32_t>({1, 1, 1, 1, 1, 1, 1, 1, 1})
    };

    const vector<uint32_t> &colOffset(uint32_t num_fields) {
        return OFFSETS[num_fields];
    }

    const vector<uint32_t> &colSize(uint32_t num_fields) {
        return SIZES[num_fields];
    }

    DataRow::~DataRow() {}

    MemDataRow::MemDataRow(uint8_t num_fields)
            : MemDataRow(OFFSETS[num_fields]) {}

    MemDataRow::MemDataRow(const vector<uint32_t> &offset) : data_(offset.back(), 0x0), offset_(offset) {}

    MemDataRow::~MemDataRow() {}

    DataField &MemDataRow::operator[](uint64_t i) {
        view_ = data_.data() + offset_[i];
        assert(i + 1 < offset_.size());
        view_.size_ = offset_[i + 1] - offset_[i];
        return view_;
    }

    void MemDataRow::operator=(DataRow &row) {
        memcpy(static_cast<void *>(data_.data()), static_cast<void *>(row.raw()),
               sizeof(uint64_t) * data_.size());
    }

    uint64_t *MemDataRow::raw() {
        return data_.data();
    }

    MemBlock::MemBlock(uint32_t size, uint32_t row_size, const vector<uint32_t> &col_offset)
            : size_(size), row_size_(row_size), col_offset_(col_offset) {
        content_ = vector<uint64_t>(size * row_size_);
    }

    MemBlock::MemBlock(uint32_t size, uint32_t row_size) : MemBlock(size, row_size, OFFSETS[row_size]) {}

    MemBlock::~MemBlock() {}

    uint64_t MemBlock::size() {
        return size_;
    }

    void MemBlock::inc(uint32_t row_to_inc) {
        content_.resize(content_.size() + row_size_ * row_to_inc);
        size_ += row_to_inc;
    }

    void MemBlock::compact(uint32_t newsize) {
        content_.resize(newsize * row_size_);
        size_ = newsize;
    }

    vector<uint64_t> &MemBlock::content() {
        return content_;
    }

    class MemDataRowIterator;

    class MemDataRowView : public DataRow {
    private:
        vector<uint64_t> &data_;
        uint64_t index_;
        uint32_t row_size_;
        const vector<uint32_t> &col_offset_;
        DataField view_;
        friend MemDataRowIterator;
    public:
        MemDataRowView(vector<uint64_t> &data, uint32_t row_size, const vector<uint32_t> &col_offset)
                : data_(data), index_(-1), row_size_(row_size), col_offset_(col_offset) {}

        virtual ~MemDataRowView() {}

        void moveto(uint64_t index) { index_ = index; }

        void next() { ++index_; }

        DataField &operator[](uint64_t i) override {
            assert(index_ < data_.size() / row_size_);
            assert(col_offset_[i] < row_size_);
            view_ = data_.data() + index_ * row_size_ + col_offset_[i];
            view_.size_ = col_offset_[i + 1] - col_offset_[i];
            return view_;
        }

        uint64_t *raw() override {
            return data_.data() + index_ * row_size_;
        }

        void operator=(DataRow &row) override {
            memcpy(static_cast<void *>(data_.data() + index_ * row_size_), static_cast<void *>(row.raw()),
                   sizeof(uint64_t) * row_size_);
        }
    };

    class MemDataRowIterator : public DataRowIterator {
    private:
        MemDataRowView reference_;
    public:
        MemDataRowIterator(vector<uint64_t> &data, uint32_t row_size, const vector<uint32_t> &col_offset)
                : reference_(data, row_size, col_offset) {}

        DataRow &operator[](uint64_t idx) override {
            reference_.moveto(idx);
            return reference_;
        }

        DataRow &next() override {
            reference_.next();
            return reference_;
        }

        uint64_t pos() override {
            return reference_.index_;
        }

        void translate(DataField &target, uint32_t col_index, uint32_t key) override {}
    };

    class MemColumnIterator : public ColumnIterator {
    private:
        vector<uint64_t> &data_;
        uint32_t row_size_;
        uint32_t col_offset_;
        uint64_t row_index_;
        DataField view_;
    public:
        MemColumnIterator(vector<uint64_t> &data, uint32_t row_size, uint32_t col_offset, uint32_t col_size)
                : data_(data), row_size_(row_size), col_offset_(col_offset), row_index_(-1) {
            view_.size_ = col_size;
        }

        DataField &operator[](uint64_t idx) override {
            assert(idx < data_.size() / row_size_);
            row_index_ = idx;
            view_ = data_.data() + idx * row_size_ + col_offset_;
            return view_;
        }

        DataField &next() override {
            view_ = data_.data() + (++row_index_) * row_size_ + col_offset_;
            return view_;
        }

        uint64_t pos() override {
            return row_index_;
        }

        void translate(DataField &, uint32_t) override {}
    };

    unique_ptr<DataRowIterator> MemBlock::rows() {
        return unique_ptr<DataRowIterator>(new MemDataRowIterator(content_, row_size_, col_offset_));
    }

    unique_ptr<ColumnIterator> MemBlock::col(uint32_t col_index) {
        return unique_ptr<ColumnIterator>(new MemColumnIterator(content_, row_size_, col_offset_[col_index],
                                                                col_offset_[col_index + 1] - col_offset_[col_index]));
    }

    shared_ptr<Block> MemBlock::mask(shared_ptr<Bitmap> mask) {
        auto newBlock = make_shared<MemBlock>(mask->cardinality(), row_size_, col_offset_);
        auto ite = mask->iterator();

        auto newData = newBlock->content_.data();
        auto oldData = content_.data();

        auto newCounter = 0;
        while (ite->hasNext()) {
            auto next = ite->next();
            memcpy((void *) (newData + (newCounter++) * row_size_),
                   (void *) (oldData + next * row_size_),
                   sizeof(uint64_t) * row_size_);
        }
        return newBlock;
    }

    MemvBlock::MemvBlock(uint32_t size, const vector<uint32_t> &col_size) : size_(size), col_size_(col_size) {
        uint8_t num_fields = col_size.size();
        for (uint8_t i = 0; i < num_fields; ++i) {
            content_.push_back(unique_ptr<vector<uint64_t>>(new vector<uint64_t>(size * col_size_[i])));
        }
    }

    MemvBlock::MemvBlock(uint32_t size, uint32_t num_fields) : MemvBlock(size, SIZES[num_fields]) {}

    MemvBlock::~MemvBlock() {}

    uint64_t MemvBlock::size() {
        return size_;
    }

    void MemvBlock::inc(uint32_t row_to_inc) {
        size_ += row_to_inc;

        uint8_t num_fields = col_size_.size();
        for (uint8_t i = 0; i < num_fields; ++i) {
            content_[i]->resize(size_ * col_size_[i]);
        }
    }

    void MemvBlock::compact(uint32_t newsize) {
        size_ = newsize;
        uint8_t num_fields = col_size_.size();
        for (uint8_t i = 0; i < num_fields; ++i) {
            content_[i]->resize(size_ * col_size_[i]);
        }
    }

    class MemvColumnIterator : public ColumnIterator {
    private:
        vector<uint64_t> &data_;
        uint64_t row_index_;
        DataField view_;
    public:
        MemvColumnIterator(vector<uint64_t> &data, uint32_t col_size)
                : data_(data), row_index_(-1) {
            assert(col_size <= 2);
            view_.size_ = col_size;
        }

        DataField &operator[](uint64_t idx) override {
            assert(idx < data_.size() / view_.size_);
            row_index_ = idx;
            view_ = data_.data() + idx * view_.size_;
            return view_;
        }

        DataField &next() override {
            ++row_index_;
            view_ = data_.data() + row_index_ * view_.size_;
            return view_;
        }

        uint64_t pos() override {
            return row_index_;
        }

        void translate(DataField &, uint32_t) override {}
    };

    class MemvDataRowIterator;

    class MemvDataRowView : public DataRow {
    private:
        unique_ptr<vector<unique_ptr<ColumnIterator>>> cols_;
        uint64_t index_;
        friend MemvDataRowIterator;
    public:
        MemvDataRowView(vector<unique_ptr<ColumnIterator>> *cols)
                : cols_(unique_ptr<vector<unique_ptr<ColumnIterator>>>(cols)), index_(-1) {}

        virtual ~MemvDataRowView() {}

        void moveto(uint64_t index) {
            index_ = index;
        }

        void next() {
            ++index_;
        }

        DataField &operator[](uint64_t i) override {
            return (*(*cols_)[i])[index_];
        }

        void operator=(DataRow &row) override {
            uint32_t num_cols = cols_->size();
            for (uint32_t i = 0; i < num_cols; ++i) {
                (*(*cols_)[i])[index_] = row[i];
            }
        }
    };

    class MemvDataRowIterator : public DataRowIterator {
    private:
        MemvDataRowView reference_;
    public:
        MemvDataRowIterator(vector<unique_ptr<ColumnIterator>> *cols)
                : reference_(cols) {}

        DataRow &operator[](uint64_t idx) override {
            reference_.moveto(idx);
            return reference_;
        }

        DataRow &next() override {
            reference_.next();
            return reference_;
        }

        uint64_t pos() override {
            return reference_.index_;
        }

        void translate(DataField &, uint32_t, uint32_t) override {}
    };


    unique_ptr<DataRowIterator> MemvBlock::rows() {
        auto cols = new vector<unique_ptr<ColumnIterator>>();
        for (auto i = 0u; i < col_size_.size(); ++i) {
            cols->push_back(col(i));
        }
        return unique_ptr<DataRowIterator>(new MemvDataRowIterator(cols));
    }

    unique_ptr<ColumnIterator> MemvBlock::col(uint32_t col_index) {
        return unique_ptr<ColumnIterator>(new MemvColumnIterator(*content_[col_index], col_size_[col_index]));
    }

    shared_ptr<Block> MemvBlock::mask(shared_ptr<Bitmap> mask) {
        // Does not support
        return nullptr;
    }

    void MemvBlock::merge(MemvBlock &another, const vector<pair<uint8_t, uint8_t>> &merge_inst) {
        this->size_ = std::max(size_, another.size_);
        for (auto &inst: merge_inst) {
            content_[inst.second] = move(another.content_[inst.first]);
        }
        // The old memblock is discarded
        another.content_.clear();
    }

    MaskedBlock::MaskedBlock(shared_ptr<Block> inner, shared_ptr<Bitmap> mask)
            : inner_(inner), mask_(mask) {}

    MaskedBlock::~MaskedBlock() {}

    uint64_t MaskedBlock::size() {
        return mask_->cardinality();
    }

    uint64_t MaskedBlock::limit() {
        return inner_->size();
    }

    class MaskedColumnIterator : public ColumnIterator {
    private:
        unique_ptr<ColumnIterator> inner_;
        unique_ptr<BitmapIterator> bite_;
    public:
        MaskedColumnIterator(unique_ptr<ColumnIterator> inner, unique_ptr<BitmapIterator> bite)
                : inner_(move(inner)), bite_(move(bite)) {}

        DataField &operator[](uint64_t index) override {
            return (*inner_)[index];
        }

        DataField &next() override {
            return (*inner_)[bite_->next()];
        }

        uint64_t pos() override {
            return inner_->pos();
        }

        void translate(DataField &, uint32_t) override {}
    };

    unique_ptr<ColumnIterator> MaskedBlock::col(uint32_t col_index) {
        return unique_ptr<MaskedColumnIterator>(new MaskedColumnIterator(inner_->col(col_index),
                                                                         mask_->iterator()));
    }

    class MaskedRowIterator : public DataRowIterator {
    private:
        unique_ptr<DataRowIterator> inner_;
        unique_ptr<BitmapIterator> bite_;
    public:
        MaskedRowIterator(unique_ptr<DataRowIterator> inner, unique_ptr<BitmapIterator> bite)
                : inner_(move(inner)), bite_(move(bite)) {}

        virtual DataRow &operator[](uint64_t index) override {
            return (*inner_)[index];
        }

        virtual DataRow &next() override {
            return (*inner_)[bite_->next()];
        }

        uint64_t pos() override {
            return inner_->pos();
        }

        void translate(DataField &, uint32_t, uint32_t) override {}
    };

    unique_ptr<DataRowIterator> MaskedBlock::rows() {
        return unique_ptr<MaskedRowIterator>(new MaskedRowIterator(inner_->rows(), mask_->iterator()));
    }

    shared_ptr<Block> MaskedBlock::mask(shared_ptr<Bitmap> mask) {
        this->mask_ = (*this->mask_) & *mask;
        return this->shared_from_this();
    }

    using namespace parquet;

    template<typename DTYPE>
    Dictionary<DTYPE>::Dictionary() {

    }

    template<typename DTYPE>
    Dictionary<DTYPE>::Dictionary(shared_ptr<DictionaryPage> dpage) {
        this->page_ = dpage;
        auto decoder = parquet::MakeTypedDecoder<DTYPE>(Encoding::PLAIN, nullptr);
        size_ = dpage->num_values();
        decoder->SetData(size_, dpage->data(), dpage->size());
        buffer_ = (T *) malloc(sizeof(T) * size_);
        decoder->Decode(buffer_, size_);
    }

    template<typename DTYPE>
    Dictionary<DTYPE>::Dictionary(T *buffer, uint32_t size) {
        this->size_ = size;
        this->buffer_ = buffer;
        this->managed_ = false;
    }

    template<typename DTYPE>
    Dictionary<DTYPE>::~Dictionary() {
        if (managed_ && nullptr != buffer_)
            free(buffer_);
    }

    template<typename DTYPE>
    int32_t Dictionary<DTYPE>::lookup(const T &key) {
        uint32_t low = 0;
        uint32_t high = size_;

        while (low <= high) {
            uint32_t mid = (low + high) >> 1;
            T midVal = buffer_[mid];

            if (midVal < key)
                low = mid + 1;
            else if (midVal > key)
                high = mid - 1;
            else
                return mid; // key found
        }
        return -(low + 1);  // key not found.
    }

    template<typename DTYPE>
    unique_ptr<vector<uint32_t>> Dictionary<DTYPE>::list(function<bool(const T &)> pred) {
        unique_ptr<vector<uint32_t>> result = unique_ptr<vector<uint32_t>>(new vector<uint32_t>());
        for (uint32_t i = 0; i < size_; ++i) {
            if (pred(buffer_[i])) {
                result->push_back(i);
            }
        }
        return result;
    }


    ParquetBlock::ParquetBlock(ParquetTable *owner, shared_ptr<RowGroupReader> rowGroup, uint32_t index,
                               uint64_t columns) : Block(index), owner_(owner), rowGroup_(rowGroup), index_(index),
                                                   columns_(columns) {}

    ParquetBlock::~ParquetBlock() {}

    Table *ParquetBlock::owner() {
        return this->owner_;
    }

    uint64_t ParquetBlock::size() {
        return rowGroup_->metadata()->num_rows();
    }

    class ParquetRowIterator;

    class ParquetRowView : public DataRow {
    protected:
        vector<unique_ptr<ColumnIterator>> &columns_;
        uint64_t index_;
        friend ParquetRowIterator;
    public:
        ParquetRowView(vector<unique_ptr<ColumnIterator>> &cols) : columns_(cols), index_(-1) {}

        virtual DataField &operator[](uint64_t colindex) override {
            return (*(columns_[colindex]))[index_];
        }

        virtual DataField &operator()(uint64_t colindex) override {
            return (*(columns_[colindex]))(index_);
        }
    };

    template<typename DTYPE>
    shared_ptr<Bitmap> ParquetBlock::raw(uint32_t col_index, RawAccessor<DTYPE> *accessor) {
        accessor->init(this->size());
        auto pageReader = rowGroup_->GetColumnPageReader(col_index);
        shared_ptr<Page> page = pageReader->NextPage();

        if (page->type() == PageType::DICTIONARY_PAGE) {
            Dictionary<DTYPE> dict(static_pointer_cast<DictionaryPage>(page));
            accessor->dict(dict);
        } else {
            accessor->data((DataPage *) page.get());
        }
        while ((page = pageReader->NextPage())) {
            accessor->data((DataPage *) page.get());
        }
        return accessor->result();
    }

    class ParquetColumnIterator;

    class ParquetRowIterator : public DataRowIterator {
    private:
        vector<unique_ptr<ColumnIterator>> columns_;
        ParquetRowView view_;
    public:
        ParquetRowIterator(ParquetBlock &block, uint64_t colindices)
                : columns_(64 - __builtin_clzl(colindices)), view_(columns_) {
            Bitset bitset(colindices);
            while (bitset.hasNext()) {
                auto index = bitset.next();
                columns_[index] = (block.col(index));
            }
        }

        virtual ~ParquetRowIterator() {
            columns_.clear();
        }

        virtual DataRow &operator[](uint64_t index) override {
            view_.index_ = index;
            return view_;
        }

        virtual DataRow &next() override {
            view_.index_++;
            return view_;
        }

        uint64_t pos() override {
            return view_.index_;
        }

        void translate(DataField &target, uint32_t col_index, uint32_t key) override {
            columns_[col_index]->translate(target, key);
        }
    };

    unique_ptr<DataRowIterator> ParquetBlock::rows() {
        return unique_ptr<DataRowIterator>(new ParquetRowIterator(*this, columns_));
    }

#define COL_BUF_SIZE 8
    const int8_t WIDTH[8] = {1, sizeof(int32_t), sizeof(int64_t), 0, sizeof(float), sizeof(double), sizeof(ByteArray),
                             0};
    const int8_t SIZE[8] = {1, 1, 1, 1, 1, 1, sizeof(ByteArray) >> 3, 0};

    class ParquetColumnIterator : public ColumnIterator {
    private:
        shared_ptr<ColumnReader> columnReader_;
        DataField dataField_;
        DataField rawField_;
        int64_t read_counter_;
        int64_t pos_;
        int64_t bufpos_;
        uint8_t width_;
        uint8_t *buffer_;
    public:
        ParquetColumnIterator(shared_ptr<ColumnReader> colReader)
                : columnReader_(colReader), dataField_(), rawField_(),
                  read_counter_(0), pos_(-1), bufpos_(-8) {
            buffer_ = (uint8_t *) malloc(sizeof(ByteArray) * COL_BUF_SIZE);
            width_ = WIDTH[columnReader_->type()];
            dataField_.size_ = SIZE[columnReader_->type()];
            rawField_.size_ = 1;
        }

        virtual ~ParquetColumnIterator() {
            free(buffer_);
        }

        virtual DataField &operator[](uint64_t idx) override {
            uint64_t *pointer = loadBuffer(idx);
            pos_ = idx;
            dataField_ = pointer;
            return dataField_;
        }

        virtual DataField &operator()(uint64_t idx) override {
            uint64_t *pointer = loadBufferRaw(idx);
            pos_ = idx;
            rawField_ = pointer;
            return rawField_;
        }

        virtual DataField &next() override {
            return (*this)[pos_ + 1];
        }

        uint64_t pos() override {
            return pos_;
        }

        void translate(DataField &target, uint32_t key) override {
            const uint8_t *dict = (const uint8_t *) columnReader_->dictionary();
            target = (uint64_t *) (dict + key * width_);
        }


    protected:
        inline uint64_t *loadBuffer(uint64_t idx) {
            if ((int64_t) idx < bufpos_ + COL_BUF_SIZE) {
                return (uint64_t *) (buffer_ + width_ * (idx - bufpos_));
            } else {
                columnReader_->MoveTo(idx);
                columnReader_->ReadBatch(COL_BUF_SIZE, nullptr, nullptr, buffer_, &read_counter_);
                bufpos_ = idx;
                return (uint64_t *) buffer_;
            }
        }

        inline uint64_t *loadBufferRaw(uint64_t idx) {
            if ((int64_t) idx < bufpos_ + COL_BUF_SIZE) {
                return (uint64_t *) (buffer_ + sizeof(int32_t) * (idx - bufpos_));
            } else {
                columnReader_->MoveTo(idx);
                columnReader_->ReadBatchRaw(COL_BUF_SIZE, reinterpret_cast<uint32_t *>(buffer_), &read_counter_);
                bufpos_ = idx;

                return (uint64_t *) buffer_;
            }
        }
    };

    unique_ptr<ColumnIterator> ParquetBlock::col(uint32_t col_index) {
        return unique_ptr<ColumnIterator>(new ParquetColumnIterator(rowGroup_->Column(col_index)));
    }

    shared_ptr<Block> ParquetBlock::mask(shared_ptr<Bitmap> mask) {
        return make_shared<MaskedBlock>(dynamic_pointer_cast<ParquetBlock>(this->shared_from_this()), mask);
    }

    uint64_t Table::size() {
        uint64_t sum = 0;
        blocks()->foreach([&sum](const shared_ptr<Block> &block) {
            sum += block->size();
        });
        return sum;
    }

    ParquetTable::ParquetTable(const string &fileName, uint64_t columns) : name_(fileName), columns_(columns) {
        fileReader_ = ParquetFileReader::OpenFile(fileName);
        if (!fileReader_) {
            throw std::invalid_argument("ParquetTable-Open: file not found");
        }
    }

    void ParquetTable::updateColumns(uint64_t columns) {
        columns_ = columns;
    }

    shared_ptr<ParquetTable> ParquetTable::Open(const string &filename, uint64_t columns) {
        return make_shared<ParquetTable>(filename, columns);
    }

    shared_ptr<ParquetTable> ParquetTable::Open(const string &filename, std::initializer_list<uint32_t> columns) {
        uint64_t ccs = 0;
        for (uint32_t c:columns) {
            ccs |= 1ul << c;
        }
        return Open(filename, ccs);
    }

    ParquetTable::~ParquetTable() {}

    using namespace std::placeholders;

    shared_ptr<Stream<shared_ptr<Block>>> ParquetTable::blocks() {
        function<shared_ptr<Block>(const int &)> mapper = bind(&ParquetTable::createParquetBlock, this, _1);
        uint32_t numRowGroups = fileReader_->metadata()->num_row_groups();
#ifdef LQF_PARALLEL
        auto stream = IntStream::Make(0, numRowGroups)->map(mapper)->parallel();
#else
        auto stream = IntStream::Make(0, numRowGroups)->map(mapper);
#endif
        return stream;
    }

    uint8_t ParquetTable::numFields() {
        return __builtin_popcount(columns_);
    }

    shared_ptr<ParquetBlock> ParquetTable::createParquetBlock(const int &block_idx) {
        auto rowGroup = fileReader_->RowGroup(block_idx);
        return make_shared<ParquetBlock>(this, rowGroup, block_idx, columns_);
    }

    MaskedTable::MaskedTable(ParquetTable *inner, unordered_map<uint32_t, shared_ptr<Bitmap>> &masks)
            : inner_(inner) {
        masks_ = vector<shared_ptr<Bitmap>>(masks.size());
        for (auto ite = masks.begin(); ite != masks.end(); ++ite) {
            masks_[ite->first] = move(ite->second);
        }
    }

    MaskedTable::~MaskedTable() {}

    using namespace std::placeholders;

    shared_ptr<Stream<shared_ptr<Block>>> MaskedTable::blocks() {
        function<shared_ptr<Block>(const shared_ptr<Block> &)> mapper =
                bind(&MaskedTable::buildMaskedBlock, this, _1);
        return inner_->blocks()->map(mapper);
    }

    uint8_t MaskedTable::numFields() {
        return inner_->numFields();
    }

    shared_ptr<Block> MaskedTable::buildMaskedBlock(const shared_ptr<Block> &input) {
        auto pblock = dynamic_pointer_cast<ParquetBlock>(input);
        return make_shared<MaskedBlock>(pblock, masks_[pblock->index()]);
    }

    TableView::TableView(uint32_t num_fields, shared_ptr<Stream<shared_ptr<Block>>> stream)
            : num_fields_(num_fields), stream_(stream) {}

    shared_ptr<Stream<shared_ptr<Block>>> TableView::blocks() {
        return stream_;
    }

    uint8_t TableView::numFields() {
        return num_fields_;
    }

    shared_ptr<MemTable> MemTable::Make(uint8_t num_fields, bool vertical) {
        return shared_ptr<MemTable>(new MemTable(lqf::colSize(num_fields), vertical));
    }

    shared_ptr<MemTable> MemTable::Make(uint8_t num_fields, uint8_t num_string_fields, bool vertical) {
        uint8_t i = 0;
        vector<uint32_t> col_size;
        for (; i < num_fields - num_string_fields; ++i) {
            col_size.push_back(1);
        }
        for (; i < num_fields; ++i) {
            col_size.push_back(2);
        }

        return shared_ptr<MemTable>(new MemTable(col_size, vertical));
    }

    shared_ptr<MemTable> MemTable::Make(const vector<uint32_t> col_size, bool vertical) {
        return shared_ptr<MemTable>(new MemTable(col_size, vertical));
    }

    MemTable::MemTable(const vector<uint32_t> col_size, bool vertical)
            : vertical_(vertical), col_size_(col_size), blocks_(vector<shared_ptr<Block>>()) {
        col_offset_.push_back(0);
        auto num_fields = col_size_.size();
        for (uint8_t k = 0; k < num_fields; ++k) {
            row_size_ += col_size_[k];
            col_offset_.push_back(col_offset_.back() + col_size_[k]);
        }
    }

    MemTable::~MemTable() {}

    uint8_t MemTable::numFields() { return col_size_.size(); }

    uint8_t MemTable::numStringFields() { return row_size_ - col_size_.size(); }

    shared_ptr<Block> MemTable::allocate(uint32_t num_rows) {
        shared_ptr<Block> block;
        if (vertical_)
            block = make_shared<MemvBlock>(num_rows, col_size_);
        else
            block = make_shared<MemBlock>(num_rows, row_size_, col_offset_);
        blocks_.push_back(block);
        return block;
    }

    void MemTable::append(shared_ptr<Block> block) {
        blocks_.push_back(block);
    }

    shared_ptr<Stream<shared_ptr<Block>>> MemTable::blocks() {
        return shared_ptr<Stream<shared_ptr<Block>>>(new VectorStream<shared_ptr<Block>>(blocks_));
    }

    const vector<uint32_t> &MemTable::colSize() { return col_size_; }

    const vector<uint32_t> &MemTable::colOffset() { return col_offset_; }

/**
 * Initialize the templates
 */
    template
    class Dictionary<Int32Type>;

    template
    class Dictionary<DoubleType>;

    template
    class Dictionary<ByteArrayType>;

    template
    class RawAccessor<Int32Type>;

    template
    class RawAccessor<DoubleType>;

    template
    class RawAccessor<ByteArrayType>;

    template shared_ptr<Bitmap>
    ParquetBlock::raw<Int32Type>(uint32_t col_index, RawAccessor<Int32Type> *accessor);

    template shared_ptr<Bitmap>
    ParquetBlock::raw<DoubleType>(uint32_t col_index, RawAccessor<DoubleType> *accessor);

    template shared_ptr<Bitmap>
    ParquetBlock::raw<ByteArrayType>(uint32_t col_index, RawAccessor<ByteArrayType> *accessor);
}