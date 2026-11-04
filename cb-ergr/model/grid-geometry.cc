/*
 * grid-geometry.cc: cell indexing, neighbour cells, centres and hysteresis margin.
 * Implements the grid of ERGR-EMHC (spec §5.1) with the 750 m / 26-neighbour rules of K§2.1.
 */

#include "grid-geometry.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

GridGeometry::GridGeometry(double boxSizeM, double cellSizeM)
    : m_boxSizeM(boxSizeM),
      m_cellSizeM(cellSizeM)
{
    // ceil with a tiny tolerance so that 3000 / 750 is exactly 4 cells, not 5.
    m_cellsPerAxis = std::max(1, static_cast<int>(std::ceil(boxSizeM / cellSizeM - 1e-9)));
}

int
GridGeometry::AxisIndex(double coordinateM) const
{
    int index = static_cast<int>(std::floor(coordinateM / m_cellSizeM));
    return std::min(std::max(index, 0), m_cellsPerAxis - 1);
}

uint16_t
GridGeometry::CellIdOf(const Vector& pos) const
{
    int ix = AxisIndex(pos.x);
    int iy = AxisIndex(pos.y);
    int iz = AxisIndex(pos.z);
    return static_cast<uint16_t>(ix + m_cellsPerAxis * (iy + m_cellsPerAxis * iz));
}

void
GridGeometry::SplitId(uint16_t cellId, int& ix, int& iy, int& iz) const
{
    ix = cellId % m_cellsPerAxis;
    iy = (cellId / m_cellsPerAxis) % m_cellsPerAxis;
    iz = cellId / (m_cellsPerAxis * m_cellsPerAxis);
}

Vector
GridGeometry::CellCentre(uint16_t cellId) const
{
    int ix, iy, iz;
    SplitId(cellId, ix, iy, iz);
    return Vector((ix + 0.5) * m_cellSizeM, (iy + 0.5) * m_cellSizeM, (iz + 0.5) * m_cellSizeM);
}

std::vector<uint16_t>
GridGeometry::NeighbourCells(uint16_t cellId, bool faceOnly) const
{
    int ix, iy, iz;
    SplitId(cellId, ix, iy, iz);
    std::vector<uint16_t> result;
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int changedAxes = (dx != 0) + (dy != 0) + (dz != 0);
                if (changedAxes == 0 || (faceOnly && changedAxes > 1))
                {
                    continue;
                }
                int nx = ix + dx;
                int ny = iy + dy;
                int nz = iz + dz;
                bool inside = nx >= 0 && ny >= 0 && nz >= 0 && nx < m_cellsPerAxis &&
                              ny < m_cellsPerAxis && nz < m_cellsPerAxis;
                if (inside)
                {
                    result.push_back(
                        static_cast<uint16_t>(nx + m_cellsPerAxis * (ny + m_cellsPerAxis * nz)));
                }
            }
        }
    }
    return result;
}

bool
GridGeometry::AreNeighbours(uint16_t a, uint16_t b, bool faceOnly) const
{
    if (a == b)
    {
        return false;
    }
    int ax, ay, az, bx, by, bz;
    SplitId(a, ax, ay, az);
    SplitId(b, bx, by, bz);
    int dx = std::abs(ax - bx);
    int dy = std::abs(ay - by);
    int dz = std::abs(az - bz);
    if (dx > 1 || dy > 1 || dz > 1)
    {
        return false;
    }
    int changedAxes = (dx != 0) + (dy != 0) + (dz != 0);
    return !faceOnly || changedAxes == 1;
}

bool
GridGeometry::AreNeighboursPaper(uint16_t a, uint16_t b) const
{
    if (a == b)
    {
        return false;
    }
    int ax, ay, az, bx, by, bz;
    SplitId(a, ax, ay, az);
    SplitId(b, bx, by, bz);
    int dx = std::abs(ax - bx), dy = std::abs(ay - by), dz = std::abs(az - bz);
    if (dx <= 1 && dy <= 1 && dz <= 1)
    {
        return true;
    }
    // two cells away along exactly one axis, same position on the other two
    return (dx == 2 && dy == 0 && dz == 0) || (dy == 2 && dx == 0 && dz == 0) || (dz == 2 && dx == 0 && dy == 0);
}

std::vector<uint16_t>
GridGeometry::NeighbourCellsPaper(uint16_t cellId) const
{
    std::vector<uint16_t> result;
    for (int other = 0; other < NumCells(); ++other)
    {
        if (AreNeighboursPaper(cellId, static_cast<uint16_t>(other)))
        {
            result.push_back(static_cast<uint16_t>(other));
        }
    }
    return result;
}

std::vector<uint8_t>
GridGeometry::HminTable(const std::vector<Vector>& sinkPositions) const
{
    const int n = NumCells();
    std::vector<int> distance(n, -1);
    std::vector<uint16_t> queue;
    for (const Vector& p : sinkPositions)
    {
        uint16_t cell = CellIdOf(p);
        if (distance[cell] < 0)
        {
            distance[cell] = 0;
            queue.push_back(cell);
        }
    }
    for (size_t head = 0; head < queue.size(); ++head)
    {
        uint16_t cell = queue[head];
        for (uint16_t next : NeighbourCellsPaper(cell))
        {
            if (distance[next] < 0)
            {
                distance[next] = distance[cell] + 1;
                queue.push_back(next);
            }
        }
    }
    std::vector<uint8_t> hmin(n, 255);
    for (int c = 0; c < n; ++c)
    {
        if (distance[c] >= 0)
        {
            hmin[c] = static_cast<uint8_t>(std::max(1, distance[c])); // the sink cell itself is one hop from the sink
        }
    }
    return hmin;
}

double
GridGeometry::MaxCentreDistanceM() const
{
    return std::sqrt(3.0) * m_cellSizeM / 2.0;
}

double
GridGeometry::InsideMarginM(const Vector& pos, uint16_t cellId) const
{
    int ix, iy, iz;
    SplitId(cellId, ix, iy, iz);
    const double lo[3] = {ix * m_cellSizeM, iy * m_cellSizeM, iz * m_cellSizeM};
    const double p[3] = {pos.x, pos.y, pos.z};
    double margin = 1e18;
    for (int axis = 0; axis < 3; ++axis)
    {
        margin = std::min(margin, p[axis] - lo[axis]);
        margin = std::min(margin, lo[axis] + m_cellSizeM - p[axis]);
    }
    return margin;
}

} // namespace cbergr
} // namespace ns3
