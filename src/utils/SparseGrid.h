/** Copyright (C) 2016 Scott Lenser - Released under terms of the AGPLv3 License */

#ifndef SPARSE_GRID_H
#define SPARSE_GRID_H

#include <cassert>
#include <unordered_map>
#include <vector>

#include "intpoint.h"
#include "SparseGridInvasive.h"

namespace cura {


namespace SparseGridImpl {

template<class T>
struct Locatoror
{
    Point operator()(const SparseGridElem<T> &elem)
    {
        return elem.point;
    }
};

} // namespace SparseGridImpl

/*! \brief Sparse grid which can locate spatially nearby values efficiently.
 *
 * \tparam Val The value type to store.
 */
template<class Val>
class SparseGrid : public SparseGridInvasive<SparseGridImpl::SparseGridElem<Val>,
                                             SparseGridImpl::Locatoror<Val> >
{
public:
    using Base = SparseGridInvasive<SparseGridImpl::SparseGridElem<Val>,
                                    SparseGridImpl::Locatoror<Val> >;

    /*! \brief Constructs a sparse grid with the specified cell size.
     *
     * \param[in] cell_size The size to use for a cell (square) in the grid.
     *    Typical values would be around 0.5-2x of expected query radius.
     * \param[in] elem_reserve Number of elements to research space for.
     * \param[in] max_load_factor Maximum average load factor before rehashing.
     */
    SparseGrid(coord_t cell_size, size_t elem_reserve=0U, float max_load_factor=1.0f);

    /*! \brief Inserts an element with specified point and value into the sparse grid.
     *
     * This is a convenience wrapper over \ref SparseGridInvasive::insert()
     *
     * \param[in] point The location for the element.
     * \param[in] val The value for the element.
     */
    void insert(const Point &point, const Val &val);

    /*! \brief Returns all values within radius of query_pt.
     *
     * Finds all values with location within radius of \p query_pt.  May
     * return additional values that are beyond radius.
     *
     * See \ref getNearby().
     *
     * \param[in] query_pt The point to search around.
     * \param[in] radius The search radius.
     * \return Vector of values found
     */
    std::vector<Val> getNearbyVals(const Point &query_pt, coord_t radius) const;

};

#define SG_TEMPLATE template<class Val>
#define SG_THIS SparseGrid<Val>

SG_TEMPLATE
SG_THIS::SparseGrid(coord_t cell_size, size_t elem_reserve, float max_load_factor) :
    Base(cell_size,elem_reserve,max_load_factor)
{
}

SG_TEMPLATE
void SG_THIS::insert(const Point &point, const Val &val)
{
    typename SG_THIS::Elem elem(point,val);
    Base::insert(elem);
}

SG_TEMPLATE
std::vector<Val>
SG_THIS::getNearbyVals(const Point &query_pt, coord_t radius) const
{
    std::vector<Val> ret;
    auto process_func = [&ret](const typename SG_THIS::Elem &elem)
        {
            ret.push_back(elem.val);
        };
    this->processNearby(query_pt, radius, process_func);
    return ret;
}


#undef SG_TEMPLATE
#undef SG_THIS

} // namespace cura

#endif // SPARSE_GRID_H
