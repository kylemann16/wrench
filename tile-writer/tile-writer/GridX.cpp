/*****************************************************************************
 *   Copyright (c) 2020, Hobu, Inc. (info@hobu.co)                           *
 *                                                                           *
 *   All rights reserved.                                                    *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 3 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 ****************************************************************************/


#include <cmath>
#include <cstdint>

#include "GridX.hpp"

using namespace pdal;

namespace untwine
{
namespace epf
{

void TileGrid::expand(const BOX3D& bounds, size_t points)
{
    m_bounds.grow(bounds);
    double xside = m_bounds.maxx - m_bounds.minx;
    double yside = m_bounds.maxy - m_bounds.miny;
    double zside = m_bounds.maxz - m_bounds.minz;
    double side = (std::max)(xside, (std::max)(yside, zside));
    m_cubicBounds = BOX3D(m_bounds.minx, m_bounds.miny, m_bounds.minz,
        m_bounds.minx + side, m_bounds.miny + side, m_bounds.minz + side);
    m_millionPoints += size_t(points / 1000000.0);


    std::cout << "EXPAND:" << m_bounds.toBox(1);
    m_gridSizeX = std::ceil(xside / m_tileLength);
    m_gridSizeY = std::ceil(yside / m_tileLength);
    std::cout << "grid size " << m_gridSizeX << " " << m_gridSizeY << std::endl;
}


TileKey TileGrid::key(double x, double y, double z) const
{
    int xi = (int)std::floor((x - m_bounds.minx) / m_tileLength);
    int yi = (int)std::floor((y - m_bounds.miny) / m_tileLength);
    xi = (std::min)((std::max)(0, xi), m_gridSizeX - 1);
    yi = (std::min)((std::max)(0, yi), m_gridSizeY - 1);
    return TileKey(xi, yi, 0);
}

} // namespace epf
} // namespace untwine
