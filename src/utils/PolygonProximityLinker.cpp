#include "PolygonProximityLinker.h"

#include <cmath> // isfinite
#include <sstream> // ostream

#include "AABB.h" // for debug output svg html
#include "SVG.h"

namespace cura 
{

PolygonProximityLinker::PolygonProximityLinker(Polygons& polygons, int proximity_distance)
 : polygons(polygons)
 , proximity_distance(proximity_distance) 
{ 
    unsigned int n_points = 0;
    for (PolygonRef poly : polygons)
    {
        n_points += poly.size();
    }

    // reserve enough elements so that iterators don't get invalidated
    proximity_point_links.reserve(n_points * 2); // generally enough, unless there are a lot of 3-way intersections in the model
    proximity_point_links_endings.reserve(n_points * 2); // any point can at most introduce two endings

    // convert to list polygons for insertion of points
    ListPolyIt::convertPolygonsToLists(polygons, list_polygons); 

    findProximatePoints();
    addProximityEndings();
    // TODO: add sharp corners

    // convert list polygons back
    ListPolyIt::convertListPolygonsToPolygons(list_polygons, polygons);
//     wallOverlaps2HTML("output/output.html");
//     list_polygons.clear(); // clear up some space! (unneccesary? it's just for the time the gcode is being generated...)
}

const PolygonProximityLinker::ProximityPointLink* PolygonProximityLinker::getLink(Point from)
{
    Point2Link::iterator from_link_pair = point_to_link.find(from);
    if (from_link_pair == point_to_link.end())
    {
        return nullptr;
    }
    return &from_link_pair->second;
}


void PolygonProximityLinker::findProximatePoints()
{
    // link each vertex of each polygon to each proximate line segment of any polygon
    // in order to avoid checking polygons twice, only compare each polygon to each previous polygon
    // and when comparing one polygon with itseld compare each vertex to each previously processed line segment
    for (unsigned int poly_idx = 0; poly_idx < list_polygons.size(); poly_idx++)
    {
        ListPolygon& poly = list_polygons[poly_idx];
        for (unsigned int poly2_idx = 0; poly2_idx <= poly_idx; poly2_idx++)
        {
            for (ListPolygon::iterator it = poly.begin(); it != poly.end(); ++it)
            {
                ListPolyIt lpi(poly, it);
                if (poly_idx == poly2_idx)
                {
//                     ListPolygon::iterator it2(it);
//                     ++it2;
//                     if (it2 != poly.end())
                    {
                        findProximatePoints(lpi, poly2_idx, it);
                    }
                }
                else 
                {
                    findProximatePoints(lpi, poly2_idx);
                }
            }
        }
    }
}

void PolygonProximityLinker::findProximatePoints(ListPolyIt from, unsigned int to_list_poly_idx)
{
    findProximatePoints(from, to_list_poly_idx, list_polygons[to_list_poly_idx].begin());
}

void PolygonProximityLinker::findProximatePoints(ListPolyIt from_it, unsigned int to_list_poly_idx, const ListPolygon::iterator start)
{
    ListPolygon& to_list_poly = list_polygons[to_list_poly_idx];
    Point& from = from_it.p();
    ListPolygon::iterator last_it = --to_list_poly.end();
    for (ListPolygon::iterator it = start; it != to_list_poly.end(); ++it)
    {
        Point& last_point = *last_it;
        Point& point = *it;

        if (&from_it.poly == &to_list_poly 
            && (
                (from_it.it == last_it || from_it.it == it) // we currently consider a linesegment directly connected to [from]
                || (from_it.prev().it == it || from_it.next().it == last_it) // line segment from [last_point] to [point] is connected to line segment of which [from] is the other end
                ) 
           )
        { 
            last_it = it;
            continue;
        }
        Point closest = LinearAlg2D::getClosestOnLineSegment(from, last_point, point);

        int64_t dist2 = vSize2(closest - from);

        if (dist2 > proximity_distance * proximity_distance
            || (&from_it.poly == &to_list_poly 
                && dot(from_it.next().p() - from, point - last_point) > 0 
                && dot(from - from_it.prev().p(), point - last_point) > 0  ) // line segments are likely connected, because the winding order is in the same general direction
        )
        { // line segment too far away to be proximate
            last_it = it;
            continue;
        }

        int64_t dist = sqrt(dist2);

        if (shorterThen(closest - last_point, 10))
        {
            addProximityLink(from_it, ListPolyIt(to_list_poly, last_it), dist);
        }
        else if (shorterThen(closest - point, 10))
        {
            addProximityLink(from_it, ListPolyIt(to_list_poly, it), dist);
        }
        else 
        {
            ListPolygon::iterator new_it = to_list_poly.insert(it, closest);
            addProximityLink(from_it, ListPolyIt(to_list_poly, new_it), dist);
        }

        last_it = it;
    }
}


bool PolygonProximityLinker::addProximityLink(ListPolyIt from, ListPolyIt to, int64_t dist)
{
    ProximityPointLink link(from, to, dist);
    std::pair<ProximityPointLinks::iterator, bool> result =
        proximity_point_links.emplace(link);

    ProximityPointLinks::iterator it = result.first;
    addToPoint2LinkMap(*it->a.it, it);
    addToPoint2LinkMap(*it->b.it, it);

    return result.second;
}

bool PolygonProximityLinker::addProximityLink_endings(ListPolyIt from, ListPolyIt to, int64_t dist)
{
    ProximityPointLink link(from, to, dist);
    std::pair<ProximityPointLinks::iterator, bool> result =
        proximity_point_links_endings.emplace(link);

//     if (! result.second)
//     {
//         DEBUG_PRINTLN("couldn't emplace in overlap_point_links! : ");
//         result.first->second = attr;
//     }

    ProximityPointLinks::iterator it = result.first;
    addToPoint2LinkMap(*it->a.it, it);
    addToPoint2LinkMap(*it->b.it, it);

    return result.second;
}

void PolygonProximityLinker::addProximityEndings()
{
    for (const ProximityPointLink& link : proximity_point_links)
    {

        if (link.dist == proximity_distance)
        { // its ending itself
            continue;
        }
        const ListPolyIt& a_1 = link.a;
        const ListPolyIt& b_1 = link.b;
        // an overlap segment can be an ending in two directions
        { 
            ListPolyIt a_2 = a_1.next();
            ListPolyIt b_2 = b_1.prev();
            addProximityEnding(link, a_2, b_2, a_2, b_1);
        }
        { 
            ListPolyIt a_2 = a_1.prev();
            ListPolyIt b_2 = b_1.next();
            addProximityEnding(link, a_2, b_2, a_1, b_2);
        }
    }
}

void PolygonProximityLinker::addProximityEnding(const ProximityPointLink& link, const ListPolyIt& a2_it, const ListPolyIt& b2_it, const ListPolyIt& a_after_middle, const ListPolyIt& b_after_middle)
{
    Point& a1 = link.a.p();
    Point& a2 = a2_it.p();
    Point& b1 = link.b.p();
    Point& b2 = b2_it.p();
    Point a = a2-a1;
    Point b = b2-b1;

    if (point_to_link.find(a2_it.p()) == point_to_link.end() 
        || point_to_link.find(b2_it.p()) == point_to_link.end())
    {
        int64_t dist = proximityEndingDistance(a1, a2, b1, b2, link.dist);
        if (dist < 0) { return; }
        int64_t a_length2 = vSize2(a);
        int64_t b_length2 = vSize2(b);
        if (dist*dist > std::min(a_length2, b_length2) )
        { // TODO remove this /\ case if error below is never shown
//             DEBUG_PRINTLN("Next point should have been linked already!!");
            dist = std::sqrt(std::min(a_length2, b_length2));
            if (a_length2 < b_length2)
            {
                Point b_p = b1 + normal(b, dist);
                ListPolygon::iterator new_b = link.b.poly.insert(b_after_middle.it, b_p);
                addProximityLink_endings(a2_it, ListPolyIt(link.b.poly, new_b), proximity_distance);
            }
            else if (b_length2 < a_length2)
            {
                Point a_p = a1 + normal(a, dist);
                ListPolygon::iterator new_a = link.a.poly.insert(a_after_middle.it, a_p);
                addProximityLink_endings(ListPolyIt(link.a.poly, new_a), b2_it, proximity_distance);
            }
            else // equal
            {
                addProximityLink_endings(a2_it, b2_it, proximity_distance);
            }
        }
        if (dist > 0)
        {
            Point a_p = a1 + normal(a, dist);
            ListPolygon::iterator new_a = link.a.poly.insert(a_after_middle.it, a_p);
            Point b_p = b1 + normal(b, dist);
            ListPolygon::iterator new_b = link.b.poly.insert(b_after_middle.it, b_p);
            addProximityLink_endings(ListPolyIt(link.a.poly, new_a), ListPolyIt(link.b.poly, new_b), proximity_distance);
        }
        else if (dist == 0)
        {
            addProximityLink_endings(link.a, link.b, proximity_distance);
        }
    }
}

int64_t PolygonProximityLinker::proximityEndingDistance(Point& a1, Point& a2, Point& b1, Point& b2, int a1b1_dist)
{
    int overlap = proximity_distance - a1b1_dist;
    Point a = a2-a1;
    Point b = b2-b1;
    double cos_angle = INT2MM2(dot(a, b)) / vSizeMM(a) / vSizeMM(b);
    // result == .5*overlap / tan(.5*angle) == .5*overlap / tan(.5*acos(cos_angle)) 
    // [wolfram alpha] == 0.5*overlap * sqrt(cos_angle+1)/sqrt(1-cos_angle)
    // [assuming positive x] == 0.5*overlap / sqrt( 2 / (cos_angle + 1) - 1 ) 
    if (cos_angle <= 0
        || ! std::isfinite(cos_angle) )
    {
        return -1; // line_width / 2;
    }
    else if (cos_angle > .9999) // values near 1 can lead too large numbers  for 1/x
    {
        return std::min(vSize(b), vSize(a));
    }
    else
    {
        int64_t dist = overlap * double ( 1.0 / (2.0 * sqrt(2.0 / (cos_angle+1.0) - 1.0)) );
        return dist;
    }
}

void PolygonProximityLinker::addSharpCorners()
{
    
}

void PolygonProximityLinker::addToPoint2LinkMap(Point p, ProximityPointLinks::iterator it)
{
    point_to_link.emplace(p, *it); // copy element from proximity_point_links set to Point2Link map
    // TODO: what to do if the map already contained a link? > three-way proximity
}


void PolygonProximityLinker::proximity2HTML(const char* filename) const
{
    PolygonProximityLinker copy = *this; // copy, cause getFlow might change the state of the overlap computation!

    AABB aabb(copy.polygons);

    aabb.expand(200);

    SVG svg(filename, aabb, Point(1024 * 2, 1024 * 2));


    svg.writeAreas(copy.polygons);

    { // output points and coords
        for (ListPolygon poly : copy.list_polygons)
        {
            for (Point& p : poly)
            {
                svg.writePoint(p, true);
            }
        }
    }

    { // output links
        // output normal links
        for (const ProximityPointLink& link : copy.proximity_point_links)
        {
            Point a = svg.transform(link.a.p());
            Point b = svg.transform(link.b.p());
            svg.printf("<line x1=\"%lli\" y1=\"%lli\" x2=\"%lli\" y2=\"%lli\" style=\"stroke:rgb(%d,%d,0);stroke-width:1\" />", a.X, a.Y, b.X, b.Y, link.dist == proximity_distance? 0 : 255, link.dist==proximity_distance? 255 : 0);
        }

        // output ending links
        for (const ProximityPointLink& link: copy.proximity_point_links_endings)
        {
            Point a = svg.transform(link.a.p());
            Point b = svg.transform(link.b.p());
            svg.printf("<line x1=\"%lli\" y1=\"%lli\" x2=\"%lli\" y2=\"%lli\" style=\"stroke:rgb(%d,%d,0);stroke-width:1\" />", a.X, a.Y, b.X, b.Y, link.dist == proximity_distance? 0 : 255, link.dist==proximity_distance? 255 : 0);
        }
    }
}

}//namespace cura 
