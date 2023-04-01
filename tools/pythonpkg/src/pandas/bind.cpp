#include "duckdb_python/pandas/pandas_bind.hpp"
#include "duckdb_python/numpy/array_wrapper.hpp"
#include "duckdb_python/pandas/pandas_analyzer.hpp"

namespace duckdb {

namespace {

struct PandasColumn {
public:
	PandasColumn(py::handle name, py::handle type, py::handle column) : name(name), type(type), handle(column) {
	}

public:
	py::handle name;
	py::handle type;
	py::handle handle;
};

struct PandasDataFrame {
public:
	PandasDataFrame(py::handle &df) {
		names = py::list(df.attr("columns"));
		types = py::list(df.attr("dtypes"));
		getter = df.attr("__getitem__");
	}
	PandasColumn operator[](idx_t index) const {
		D_ASSERT(index < names.size());
		auto column = getter(names[index]);
		auto type = types[index];
		auto name = names[index];
		return PandasColumn(name, type, column);
	}

public:
	py::list names;
	py::list types;

private:
	py::object getter;
};

}; // namespace

static LogicalType BindColumn(PandasColumn column_p, PandasColumnBindData &bind_data, const DBConfig &config) {
	LogicalType column_type;
	auto &column = column_p.handle;

	bind_data.numpy_type = ConvertNumpyType(column_p.type);
	bool column_has_mask = py::hasattr(column.attr("array"), "_mask");

	if (column_has_mask) {
		// masked object, fetch the internal data and mask array
		bind_data.mask = make_unique<RegisteredArray>(column.attr("array").attr("_mask"));
	}

	if (bind_data.numpy_type == NumpyNullableType::CATEGORY) {
		// for category types, we create an ENUM type for string or use the converted numpy type for the rest
		D_ASSERT(py::hasattr(column, "cat"));
		D_ASSERT(py::hasattr(column.attr("cat"), "categories"));
		auto categories = py::array(column.attr("cat").attr("categories"));
		auto categories_pd_type = ConvertNumpyType(categories.attr("dtype"));
		if (categories_pd_type == NumpyNullableType::OBJECT) {
			// Let's hope the object type is a string.
			bind_data.numpy_type = NumpyNullableType::CATEGORY;
			auto enum_name = string(py::str(column_p.name));
			vector<string> enum_entries = py::cast<vector<string>>(categories);
			idx_t size = enum_entries.size();
			Vector enum_entries_vec(LogicalType::VARCHAR, size);
			auto enum_entries_ptr = FlatVector::GetData<string_t>(enum_entries_vec);
			for (idx_t i = 0; i < size; i++) {
				enum_entries_ptr[i] = StringVector::AddStringOrBlob(enum_entries_vec, enum_entries[i]);
			}
			D_ASSERT(py::hasattr(column.attr("cat"), "codes"));
			column_type = LogicalType::ENUM(enum_name, enum_entries_vec, size);
			bind_data.numpy_col = py::array(column.attr("cat").attr("codes"));
			D_ASSERT(py::hasattr(bind_data.numpy_col, "dtype"));
			bind_data.internal_categorical_type = string(py::str(bind_data.numpy_col.attr("dtype")));
		} else {
			bind_data.numpy_col = py::array(column.attr("to_numpy")());
			auto numpy_type = bind_data.numpy_col.attr("dtype");
			// for category types (non-strings), we use the converted numpy type
			bind_data.numpy_type = ConvertNumpyType(numpy_type);
			column_type = NumpyToLogicalType(bind_data.numpy_type);
		}
	} else if (bind_data.numpy_type == NumpyNullableType::FLOAT_16) {
		auto pandas_array = column.attr("array");
		bind_data.numpy_col = py::array(column.attr("to_numpy")("float32"));
		bind_data.numpy_type = NumpyNullableType::FLOAT_32;
		column_type = NumpyToLogicalType(bind_data.numpy_type);
	} else {
		auto pandas_array = column.attr("array");
		if (py::hasattr(pandas_array, "_data")) {
			// This means we can access the numpy array directly
			bind_data.numpy_col = column.attr("array").attr("_data");
		} else if (py::hasattr(pandas_array, "asi8")) {
			// This is a datetime object, has the option to get the array as int64_t's
			bind_data.numpy_col = py::array(pandas_array.attr("asi8"));
		} else {
			// Otherwise we have to get it through 'to_numpy()'
			bind_data.numpy_col = py::array(column.attr("to_numpy")());
		}
		column_type = NumpyToLogicalType(bind_data.numpy_type);
	}
	// Analyze the inner data type of the 'object' column
	if (bind_data.numpy_type == NumpyNullableType::OBJECT) {
		PandasAnalyzer analyzer(config);
		if (analyzer.Analyze(column)) {
			column_type = analyzer.AnalyzedType();
		}
	}
	return column_type;
}

void Pandas::Bind(const DBConfig &config, py::handle df_p, vector<PandasColumnBindData> &bind_columns,
                  vector<LogicalType> &return_types, vector<string> &names) {

	PandasDataFrame df(df_p);
	// TODO support masked arrays as well
	// TODO support dicts of numpy arrays as well
	idx_t column_count = py::len(df.names);
	if (column_count == 0 || py::len(df.types) == 0 || column_count != py::len(df.types)) {
		throw InvalidInputException("Need a DataFrame with at least one column");
	}

	return_types.reserve(column_count);
	names.reserve(column_count);
	// loop over every column
	for (idx_t col_idx = 0; col_idx < column_count; col_idx++) {
		PandasColumnBindData bind_data;

		names.emplace_back(py::str(df.names[col_idx]));
		auto column_type = BindColumn(df[col_idx], bind_data, config);

		D_ASSERT(py::hasattr(bind_data.numpy_col, "strides"));
		bind_data.numpy_stride = bind_data.numpy_col.attr("strides").attr("__getitem__")(0).cast<idx_t>();
		return_types.push_back(column_type);
		bind_columns.push_back(std::move(bind_data));
	}
}

} // namespace duckdb
