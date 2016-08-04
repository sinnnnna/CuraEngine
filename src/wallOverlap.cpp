/** Copyright (C) 2016 Ultimaker - Released under terms of the AGPLv3 License */
#include "wallOverlap.h"

#include <cmath> // isfinite
#include <sstream>

#include "utils/AABB.h" // for debug output svg html
#include "utils/SVG.h"

namespace cura 
{

WallOverlapComputation::WallOverlapComputation(Polygons& polygons, int line_width)
: overlap_linker(polygons, line_width)
, line_width(line_width)
{ 

}


float WallOverlapComputation::getFlow(Point& from, Point& to)
{
    using Point2LinkIt = PolygonProximityLinker::Point2Link::iterator;

    if (!overlap_linker.isLinked(from))
    { // [from] is not linked
        return 1;
    }
    const std::pair<Point2LinkIt, Point2LinkIt> to_links = overlap_linker.getLinks(to);
    if (to_links.first == to_links.second)
    { // [to] is not linked
        return 1;
    }

    int64_t overlap_area = 0;
    // note that we don't need to loop over all from_links, because they are handled in the previous getFlow(.) call (or in the very last)
    for (Point2LinkIt to_link_it = to_links.first; to_link_it != to_links.second; ++to_link_it)
    {
        const ProximityPointLink& to_link = to_link_it->second;
        ListPolyIt to_it = to_link.a;
        ListPolyIt to_other_it = to_link.b;
        if (to_link.a.p() != to)
        {
            assert(to_link.b.p() == to && "Either part of the link should be the point in the link!");
            std::swap(to_it, to_other_it);
        }
        ListPolyIt from_it = to_it.prev();
        assert(from_it.p() == from && "From location doesn't seem to be connected to destination location!");

        ListPolyIt to_other_next_it = to_other_it.next(); // move towards [from]; the lines on the other side move in the other direction
        //           to  from
        //   o<--o<--T<--F
        //   |       :   :
        //   v       :   :
        //   o-->o-->o-->o
        //           ,   ,
        //           ;   to_other_next
        //           to other

        // handle multiple points  linked to [to]
        //   o<<<T<<<F
        //     / |
        //    /  |
        //   o>>>o>>>o
        //   ,   ,
        //   ;   to other next
        //   to other
        overlap_area += handlePotentialOverlap(to_link, to_other_next_it, to_it);

        // handle multiple points  linked to [to_other]
        //   o<<<T<<<F
        //       |  /
        //       | /
        //   o>>>o>>>o
        overlap_area += handlePotentialOverlap(to_link, to_other_it, from_it);

        // handle normal case where the segment from-to overlaps with another segment
        //   o<<<T<<<F
        //       |   |
        //       |   |
        //   o>>>o>>>o
        //       ,   ,
        //       ;   to other next
        //       to other
        overlap_area += handlePotentialOverlap(to_link, to_other_next_it, from_it);
    }

    int64_t normal_area = vSize(from - to) * line_width;
    float ratio = float(normal_area - overlap_area) / normal_area;
    if (ratio > 1.0)
    {
        return 1.0;
    }
    if (ratio < 0.0)
    {
        return 0;
    }
    return ratio;
}

int64_t WallOverlapComputation::handlePotentialOverlap(const ProximityPointLink& link_a, const ListPolyIt from_it, const ListPolyIt to_it)
{
    const ProximityPointLink* link_b = overlap_linker.getLink(from_it, to_it);
    if (!link_b)
    {
        return 0;
    }
    if (!getIsPassed(link_a, *link_b))
    { // check whether the segment is already passed
        setIsPassed(link_a, *link_b);
        return 0;
    }
    // mark the segment as passed
    setIsPassed(link_a, *link_b);
    return getApproxOverlapArea(link_a, *link_b);
}


int64_t WallOverlapComputation::getApproxOverlapArea(const ProximityPointLink& from, const ProximityPointLink& to)
{
    return getApproxOverlapArea(from.a.p(), from.b.p(), from.dist, to.a.p(), to.b.p(), to.dist);
}


int64_t WallOverlapComputation::getApproxOverlapArea(const Point from_a, const Point from_b, const int64_t from_dist, const Point to_a, const Point to_b, const int64_t to_dist)
{
    const Point from_middle = from_a + from_b; // dont divide by two just yet
    const Point to_middle = to_a + to_b; // dont divide by two just yet

    const int64_t dist_2 = vSize(from_middle - to_middle);

    const int64_t average_dists_2 = line_width * 2 - from_dist - to_dist; // dont divide by two just yet

    const int64_t area = dist_2 * average_dists_2 / 4; // divide by 2 two times: once for the middles and once for the average_dists
    return area;
}

bool WallOverlapComputation::getIsPassed(const ProximityPointLink& link_a, const ProximityPointLink& link_b)
{
    return passed_links.count(SymmetricPair<ProximityPointLink>(link_a, link_b)) > 0;
}

void WallOverlapComputation::setIsPassed(const ProximityPointLink& link_a, const ProximityPointLink& link_b)
{
    passed_links.emplace(link_a, link_b);
}


}//namespace cura 
