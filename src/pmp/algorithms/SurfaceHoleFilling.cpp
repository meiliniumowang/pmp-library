//=============================================================================

#include <pmp/algorithms/SurfaceHoleFilling.h>
#include <pmp/algorithms/SurfaceFairing.h>

#include <Eigen/Dense>
#include <Eigen/Sparse>

using SparseMatrix = Eigen::SparseMatrix<double>;
using Triplet = Eigen::Triplet<double>;

//=============================================================================

namespace pmp {

//=============================================================================

SurfaceHoleFilling::SurfaceHoleFilling(SurfaceMesh& _mesh) : mesh_(_mesh)
{
    points_ = mesh_.vertex_property<Point>("v:point");
}

//-----------------------------------------------------------------------------

bool SurfaceHoleFilling::is_interior_edge(Vertex _a, Vertex _b) const
{
    Halfedge h = mesh_.find_halfedge(_a, _b);
    if (!h.is_valid())
        return false; // edge does not exist
    return (!mesh_.is_boundary(h) &&
            !mesh_.is_boundary(mesh_.opposite_halfedge(h)));
}

//-----------------------------------------------------------------------------

Scalar SurfaceHoleFilling::compute_area(Vertex _a, Vertex _b, Vertex _c) const
{
    return sqrnorm(cross(points_[_b] - points_[_a], points_[_c] - points_[_a]));
}

//-----------------------------------------------------------------------------

Point SurfaceHoleFilling::compute_normal(Vertex _a, Vertex _b, Vertex _c) const
{
    return normalize(
        cross(points_[_b] - points_[_a], points_[_c] - points_[_a]));
}

//-----------------------------------------------------------------------------

Scalar SurfaceHoleFilling::compute_angle(const Point& _n1,
                                         const Point& _n2) const
{
    return (1.0 - dot(_n1, _n2));
}

//-----------------------------------------------------------------------------

bool SurfaceHoleFilling::fill_hole(Halfedge _h)
{
    // is it really a hole?
    if (!mesh_.is_boundary(_h))
    {
        return false;
    }

    bool ok = false;

    // lock vertices/edge that already exist, to be later able to
    // identify the filled-in vertices/edges
    vlocked_ =
        mesh_.add_vertex_property<bool>("SurfaceHoleFilling:vlocked", false);
    elocked_ =
        mesh_.add_edge_property<bool>("SurfaceHoleFilling:elocked", false);
    for (auto v : mesh_.vertices())
        vlocked_[v] = true;
    for (auto e : mesh_.edges())
        elocked_[e] = true;

    // first do minimal triangulation
    if (triangulate_hole(_h))
    {
        // refine filled-in edges
        refine();
        ok = true;
    }

    // clean up
    hole_.clear();
    mesh_.remove_vertex_property(vlocked_);
    mesh_.remove_edge_property(elocked_);

    return ok;
}

//-----------------------------------------------------------------------------

bool SurfaceHoleFilling::triangulate_hole(Halfedge _h)
{
    // trace hole
    hole_.clear();
    Halfedge h = _h;
    do
    {
        // check for manifoldness
        if (!mesh_.is_manifold(mesh_.to_vertex(h)))
        {
            std::cerr << "[SurfaceHoleFilling] Non-manifold hole\n";
            return false;
        }

        hole_.push_back(h);
    } while ((h = mesh_.next_halfedge(h)) != _h);
    const int n = hole_.size();
    std::cout << n << " hole edges\n";

    // compute minimal triangulation by dynamic programming
    weight_.clear();
    weight_.resize(n, std::vector<Weight>(n, Weight()));
    index_.clear();
    index_.resize(n, std::vector<int>(n, 0));

    int i, j, m, k, imin;
    Weight w, wmin;

    // initialize 2-gons
    for (i = 0; i < n - 1; ++i)
    {
        weight_[i][i + 1] = Weight(0, 0);
        index_[i][i + 1] = -1;
    }

    // n-gons with n>2
    for (j = 2; j < n; ++j)
    {
        // for all n-gons [i,i+j]
        for (i = 0; i < n - j; ++i)
        {
            k = i + j;
            wmin = Weight();
            imin = -1;

            // find best split i < m < i+j
            for (m = i + 1; m < k; ++m)
            {
                w = weight_[i][m] + compute_weight(i, m, k) + weight_[m][k];
                if (w < wmin)
                {
                    wmin = w;
                    imin = m;
                }
            }

            weight_[i][k] = wmin;
            index_[i][k] = imin;
        }
    }

    // now add triangles to mesh
    std::vector<ivec2> todo;
    todo.reserve(n);
    todo.push_back(ivec2(0, n - 1));
    while (!todo.empty())
    {
        ivec2 tri = todo.back();
        todo.pop_back();
        int start = tri[0];
        int end = tri[1];
        if (end - start < 2)
            continue;
        int split = index_[start][end];

        mesh_.add_triangle(hole_vertex(start), hole_vertex(split),
                           hole_vertex(end));

        todo.push_back(ivec2(start, split));
        todo.push_back(ivec2(split, end));
    }

    // clean up
    weight_.clear();
    index_.clear();

    return true;
}

//-----------------------------------------------------------------------------

SurfaceHoleFilling::Weight SurfaceHoleFilling::compute_weight(int _i, int _j,
                                                              int _k) const
{
    const Vertex a = hole_vertex(_i);
    const Vertex b = hole_vertex(_j);
    const Vertex c = hole_vertex(_k);
    Vertex d;

    // if one of the potential edges already exists, this would result
    // in an invalid triangulation -> prevent by giving infinite weight
    if (is_interior_edge(a, b) || is_interior_edge(b, c) ||
        is_interior_edge(c, a))
    {
        return Weight();
    }

    // compute area
    const Scalar area = compute_area(a, b, c);

    // compute dihedral angles with...
    Scalar angle(0);
    const Point n = compute_normal(a, b, c);

    // ...neighbor to (i,j)
    d = (_i + 1 == _j) ? opposite_vertex(_j) : hole_vertex(index_[_i][_j]);
    angle = std::max(angle, compute_angle(n, compute_normal(a, d, b)));

    // ...neighbor to (j,k)
    d = (_j + 1 == _k) ? opposite_vertex(_k) : hole_vertex(index_[_j][_k]);
    angle = std::max(angle, compute_angle(n, compute_normal(b, d, c)));

    // ...neighbor to (k,i) if (k,i)==(n-1, 0)
    if (_i == 0 && _k + 1 == (int)hole_.size())
    {
        d = opposite_vertex(0);
        angle = std::max(angle, compute_angle(n, compute_normal(c, d, a)));
    }

    return Weight(angle, area);
}

//-----------------------------------------------------------------------------

void SurfaceHoleFilling::refine()
{
    const int n = hole_.size();
    Scalar l, lmin, lmax;

    // compute target edge length
    l = 0.0;
    for (int i = 0; i < n; ++i)
    {
        l += distance(points_[hole_vertex(i)],
                      points_[hole_vertex((i + 1) % n)]);
    }
    l /= (Scalar)n;
    lmin = 0.7 * l;
    lmax = 1.5 * l;

    // do some iterations
    for (int iter = 0; iter < 10; ++iter)
    {
        split_long_edges(lmax);
        collapse_short_edges(lmin);
        flip_edges();
        relaxation();
    }
    fairing();
}

//-----------------------------------------------------------------------------

void SurfaceHoleFilling::split_long_edges(const Scalar _lmax)
{
    bool ok;
    int i;

    for (ok = false, i = 0; !ok && i < 10; ++i)
    {
        ok = true;

        for (auto e : mesh_.edges())
        {
            if (!elocked_[e])
            {
                Halfedge h10 = mesh_.halfedge(e, 0);
                Halfedge h01 = mesh_.halfedge(e, 1);
                const Point& p0 = points_[mesh_.to_vertex(h10)];
                const Point& p1 = points_[mesh_.to_vertex(h01)];

                if (distance(p0, p1) > _lmax)
                {
                    mesh_.split(e, 0.5 * (p0 + p1));
                    ok = false;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------

void SurfaceHoleFilling::collapse_short_edges(const Scalar _lmin)
{
    bool ok;
    int i;

    for (ok = false, i = 0; !ok && i < 10; ++i)
    {
        ok = true;

        for (auto e : mesh_.edges())
        {
            if (!mesh_.is_deleted(e) && !elocked_[e])
            {
                Halfedge h10 = mesh_.halfedge(e, 0);
                Halfedge h01 = mesh_.halfedge(e, 1);
                Vertex v0 = mesh_.to_vertex(h10);
                Vertex v1 = mesh_.to_vertex(h01);
                const Point& p0 = points_[v0];
                const Point& p1 = points_[v1];

                // edge too short?
                if (distance(p0, p1) < _lmin)
                {
                    Halfedge h;
                    if (!vlocked_[v0])
                        h = h01;
                    else if (!vlocked_[v1])
                        h = h10;

                    if (h.is_valid() && mesh_.is_collapse_ok(h))
                    {
                        mesh_.collapse(h);
                        ok = false;
                    }
                }
            }
        }
    }

    mesh_.garbage_collection();
}

//-----------------------------------------------------------------------------

void SurfaceHoleFilling::flip_edges()
{
    Vertex v0, v1, v2, v3;
    Halfedge h;
    int val0, val1, val2, val3;
    int val_opt0, val_opt1, val_opt2, val_opt3;
    int ve0, ve1, ve2, ve3, ve_before, ve_after;
    bool ok;
    int i;

    for (ok = false, i = 0; !ok && i < 10; ++i)
    {
        ok = true;

        for (auto e : mesh_.edges())
        {
            if (!elocked_[e])
            {
                h = mesh_.halfedge(e, 0);
                v0 = mesh_.to_vertex(h);
                v2 = mesh_.to_vertex(mesh_.next_halfedge(h));
                h = mesh_.halfedge(e, 1);
                v1 = mesh_.to_vertex(h);
                v3 = mesh_.to_vertex(mesh_.next_halfedge(h));

                val0 = mesh_.valence(v0);
                val1 = mesh_.valence(v1);
                val2 = mesh_.valence(v2);
                val3 = mesh_.valence(v3);

                val_opt0 = (mesh_.is_boundary(v0) ? 4 : 6);
                val_opt1 = (mesh_.is_boundary(v1) ? 4 : 6);
                val_opt2 = (mesh_.is_boundary(v2) ? 4 : 6);
                val_opt3 = (mesh_.is_boundary(v3) ? 4 : 6);

                ve0 = (val0 - val_opt0);
                ve1 = (val1 - val_opt1);
                ve2 = (val2 - val_opt2);
                ve3 = (val3 - val_opt3);

                ve_before = ve0 * ve0 + ve1 * ve1 + ve2 * ve2 + ve3 * ve3;

                --val0;
                --val1;
                ++val2;
                ++val3;

                ve0 = (val0 - val_opt0);
                ve1 = (val1 - val_opt1);
                ve2 = (val2 - val_opt2);
                ve3 = (val3 - val_opt3);

                ve_after = ve0 * ve0 + ve1 * ve1 + ve2 * ve2 + ve3 * ve3;

                if (ve_before > ve_after && mesh_.is_flip_ok(e))
                {
                    mesh_.flip(e);
                    ok = false;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------

void SurfaceHoleFilling::relaxation()
{
    // properties
    VertexProperty<int> idx =
        mesh_.add_vertex_property<int>("SurfaceHoleFilling:idx", -1);

    // collect free vertices
    std::vector<Vertex> vertices;
    vertices.reserve(mesh_.n_vertices());
    for (auto v : mesh_.vertices())
    {
        if (!vlocked_[v])
        {
            idx[v] = vertices.size();
            vertices.push_back(v);
        }
    }
    const int n = vertices.size();

    // collect constraints
    std::vector<Vertex> constraints;
    constraints.reserve(mesh_.n_vertices());
    for (auto v : mesh_.vertices())
    {
        if (!vlocked_[v])
        {
            constraints.push_back(v);
        }
    }
    for (auto h : hole_)
    {
        constraints.push_back(mesh_.to_vertex(h));
    }
    const int m = constraints.size();

    // setup matrix & rhs
    Eigen::MatrixXd B(m, 3);
    std::vector<Triplet> triplets;
    for (int i = 0; i < m; ++i)
    {
        Vertex v = constraints[i];
        Point b(0, 0, 0);
        Scalar c(0);

        for (auto vv : mesh_.vertices(v))
        {
            if (vlocked_[vv])
                b += points_[vv];
            else
                triplets.emplace_back(i, idx[vv], -1.0);
            ++c;
        }

        if (vlocked_[v])
            b -= c * points_[v];
        else
            triplets.emplace_back(i, idx[v], c);

        B(i, 0) = b[0];
        B(i, 1) = b[1];
        B(i, 2) = b[2];
    }

    // solve least squares system
    SparseMatrix A(m, n);
    A.setFromTriplets(triplets.begin(), triplets.end());
    SparseMatrix AtA = A.transpose() * A;
    Eigen::MatrixXd AtB = A.transpose() * B;
    Eigen::SimplicialLDLT<SparseMatrix> solver(AtA);
    Eigen::MatrixXd X = solver.solve(AtB);
    if (solver.info() != Eigen::Success)
    {
        std::cerr << "[SurfaceHoleFilling] Solver failed\n";
        return;
    }

    // copy solution to mesh vertices
    for (int i = 0; i < n; ++i)
    {
        Vertex v = vertices[i];
        points_[v][0] = X(i, 0);
        points_[v][1] = X(i, 1);
        points_[v][2] = X(i, 2);
    }

    // clean up
    mesh_.remove_vertex_property(idx);
}

//-----------------------------------------------------------------------------

void SurfaceHoleFilling::fairing()
{
    // did the refinement insert new vertices?
    // if yes, then trigger fairing; otherwise don't.
    bool new_vertices = false;
    for (auto v : mesh_.vertices())
        if (!vlocked_[v])
            new_vertices = true;
    if (!new_vertices)
        return;

    // convert non-locked into selection
    auto vsel = mesh_.add_vertex_property<bool>("v:selected");
    for (auto v : mesh_.vertices())
        vsel[v] = !vlocked_[v];

    // fair new vertices
    SurfaceFairing fairing(mesh_);
    fairing.minimize_curvature();

    // clean up
    mesh_.remove_vertex_property(vsel);
}

//=============================================================================
}
//=============================================================================
