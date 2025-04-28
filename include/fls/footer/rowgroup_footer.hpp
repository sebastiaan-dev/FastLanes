#ifndef FLS_FOOTER_ROWGROUP_FOOTER_HPP
#define FLS_FOOTER_ROWGROUP_FOOTER_HPP

#include "fls/footer/column_descriptor.hpp"
#include "fls/std/filesystem.hpp"
#include "fls/std/unordered_map.hpp"

namespace fastlanes {
/*--------------------------------------------------------------------------------------------------------------------*/
class Rowgroup;
enum class DataType : uint8_t;
/*--------------------------------------------------------------------------------------------------------------------*/

using map                       = unordered_map<string, idx_t>;
using col_descripption_it       = vector<ColumnDescriptor>::iterator;
using const_col_descripption_it = vector<ColumnDescriptor>::const_iterator;

/**
 * The footer contains metadata for the associated Rowgroup, required for materializing the referenced data.
 */
class Footer {
public:
	Footer();
	Footer(const Footer&)              = default;
	Footer(Footer&&)                   = default;
	Footer& operator=(const Footer&) & = default;
	Footer& operator=(Footer&&) &      = default;
	~Footer()                          = default;

public:
	//! Get a const reference to the columns.
	[[nodiscard]] const ColumnDescriptors& GetColumnDescriptors() const { return m_column_descriptors; }
	//! @todo remove?
	void AddCol(const ColumnDescriptor& col);
	//! Overload the default [] operator.
	ColumnDescriptor& operator[](n_t idx);
	//! Overload the default const [] operator.
	const ColumnDescriptor& operator[](n_t idx) const;
	//! Get the idx of the next available column.
	[[nodiscard]] n_t GetNextColIdx() const;
	//! Get the column count.
	[[nodiscard]] n_t size() const;
	//! Get the mapping for column names to idxs.
	[[nodiscard]] map GetMap() const;
	//! @todo correct spelling, is this used?
	col_descripption_it                     begin();
	[[nodiscard]] const_col_descripption_it begin() const;
	col_descripption_it                     end();
	[[nodiscard]] const_col_descripption_it end() const;
	//! Get the idx associated with the column name.
	[[nodiscard]] idx_t LookUp(const string& name) const;
	//! Projecting discards irrelevant columns by no longer tracking them, this allows column skipping.
	[[nodiscard]] up<Footer> Project(const vector<idx_t>& idxs) const;
	//! Add a column to the Footer @todo remove?
	void push_back(ColumnDescriptor&&);
	//! Get the names of all (projected) columns.
	[[nodiscard]] vector<string> GetColumnNames() const;
	//! Get the type for the tuples in each (projected) column.
	[[nodiscard]] vector<DataType> GetDataTypes() const;
	//! Get the number of available vectors.
	[[nodiscard]] n_t GetNVectors() const;

public:
	//! When reading a FastLanes file, it represents the amount of vectors stored in the file. Otherwise, it represents
	//! the amount of materialized vectors in the Rowgroup.
	n_t m_n_vec;
	//! Array of columns in the associated Rowgroup.
	ColumnDescriptors m_column_descriptors;
	//! A map from column names to the respective idx, this is required when projecting since the set of available idxs
	//! has changed. @fixme This is empty until a projection is made, this fails if you project using strings first.
	map m_name_idx_map;
	//! The binary size of the associated Rowgroup
	sz_t m_rowgroup_size;
};

//! Create a footer from a Rowgroup, used when data has been read from an external format.
up<Footer> make_footer(const Rowgroup& rowgroup);
//! Create a footer from a JSON file, used when reading a FastLanes file.
up<Footer> make_footer(const path& dir_path);

} // namespace fastlanes

#endif // FLS_FOOTER_ROWGROUP_FOOTER_HPP
