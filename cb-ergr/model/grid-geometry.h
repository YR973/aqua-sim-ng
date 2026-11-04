/*
 * grid-geometry.h: the 3D grid of cubic cells (ERGR-EMHC, spec §5.1; K§2.1).
 *
 * The simulation box [0, box]^3 is cut into cubes of side cellSizeM. A cell has an id
 * (ix + n*(iy + n*iz), n = cells per axis). Positions outside the box are clamped to
 * the border cells. z is DEPTH in metres (0 = surface), so smaller z is closer to the sinks.
 * Plain C++ (uses ns3::Vector only as a 3-number struct).
 */

#ifndef CB_ERGR_GRID_GEOMETRY_H
#define CB_ERGR_GRID_GEOMETRY_H

#include "ns3/vector.h"

#include <cstdint>
#include <vector>

namespace ns3
{
namespace cbergr
{

class GridGeometry
{
  public:
    GridGeometry(double boxSizeM, double cellSizeM);

    int CellsPerAxis() const { return m_cellsPerAxis; }
    int NumCells() const { return m_cellsPerAxis * m_cellsPerAxis * m_cellsPerAxis; }
    double CellSizeM() const { return m_cellSizeM; }

    /// Cell containing 'pos' (clamped into the box).
    uint16_t CellIdOf(const Vector& pos) const;

    /// Geometric centre of a cell.
    Vector CellCentre(uint16_t cellId) const;

    /// Cells that touch 'cellId' (26 around an interior cell; 6 if faceOnly). Border cells have fewer.
    std::vector<uint16_t> NeighbourCells(uint16_t cellId, bool faceOnly) const;

    /// True if b is one of the neighbour cells of a (never true for a == b).
    bool AreNeighbours(uint16_t a, uint16_t b, bool faceOnly) const;

    /// Largest possible distance from a point in the cell to its centre (half the cube diagonal), metres.
    double MaxCentreDistanceM() const;

    /**
     * How far 'pos' lies inside cell 'cellId': distance to the nearest of the six faces.
     * Negative if pos is outside the cell. Used for the membership hysteresis (D§3 P3).
     */
    double InsideMarginM(const Vector& pos, uint16_t cellId) const;

  private:
    void SplitId(uint16_t cellId, int& ix, int& iy, int& iz) const;
    int AxisIndex(double coordinateM) const;

    double m_boxSizeM;
    double m_cellSizeM;
    int m_cellsPerAxis;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_GRID_GEOMETRY_H */
