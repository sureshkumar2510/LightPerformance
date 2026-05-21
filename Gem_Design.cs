using System;
using System.Collections.Generic;
using System.Linq;
using Rhino;
using Rhino.Geometry;
using Rhino.DocObjects;
using Rhino.Input;
using Rhino.Input.Custom;

RhinoDoc doc = RhinoDoc.ActiveDoc;

public class DiamondProportions
{
    public double TablePercentage {get; set;}
    public double GirdlePercentage {get; set;}
    public double PavillionPercentage {get; set;}
    public double CrownPercentage {get; set;}
    public double CrownAngle {get; set;}
    public double PavillionAngle {get; set;}

    public Polyline Table {get; set;}
    public Polyline BezelFacet {get; set;}
    public Polyline PavillionFacet {get; set;}
    public Polyline StarFacet {get; set;}
    public Polyline UpperGirdleFacet {get; set;}
    public Polyline LowerGirdleFacet {get; set;}

    //Diamond Proportion Parameters
    public DiamondProportions(double tablePercentage, double girdlePercentage, double pavillionPercentage, double crownPercentage, double crownAngle, double pavillionAngle)
    {
        TablePercentage = tablePercentage;
        GirdlePercentage = girdlePercentage;
        PavillionPercentage = pavillionPercentage;
        CrownPercentage = crownPercentage;
        CrownAngle = crownAngle;
        PavillionAngle = pavillionAngle;
    }

    //Creation of Table Polyline from the Proportion
    public void TableCreation(double gemDiameter)
    {
        double tableRatio = TablePercentage > 1 ? TablePercentage/100.0 : TablePercentage;
        double tableDiameter = tableRatio * gemDiameter;
        Circle tableCircle = new Circle(tableDiameter/2.0);
        Table = Polyline.CreateInscribedPolygon(tableCircle, 8);
        return;   
    }

    //Creation of Bezel Facet Polyline from Proportion
    public void BezelFacetCreation(double gemDiameter)
    {
        List<Point3d> bezelPts = new List<Point3d>();
        
        Point3d origin = Point3d.Origin;
        Point3d firstBezelPt = new Point3d(gemDiameter/2, 0, 0);     //First Point - Bezel Facet polyline start
        bezelPts.Add(firstBezelPt);

        double tableRatio = TablePercentage > 1 ? TablePercentage/100.0 : TablePercentage;
        double tableDiameter = tableRatio * gemDiameter;

        double radians = 22.5 * Math.PI / 180;
        double radius = gemDiameter / 2.0;
        Point3d rotatedRightPt = new Point3d(Math.Cos(radians) * radius, Math.Sin(radians) * radius, 0);

        double lengthPerp = tableDiameter/2 * Math.Cos(radians);
        Point3d rotatedLeftPt = new Point3d(Math.Cos(radians) * lengthPerp, Math.Sin(radians) * lengthPerp, 0);

        Point3d secondBezelPt = (rotatedRightPt + rotatedLeftPt)/2;     //Second point - MidPoint of 2 rotated points at 22.5 degrees -- Midpoint because of 50:50 split for Crown Facets
        bezelPts.Add(secondBezelPt);

        Point3d thirdBezelPt = new Point3d(tableDiameter/2, 0, 0);      //Third point - On the Table / Table Circle
        bezelPts.Add(thirdBezelPt);

        Transform mirror = Transform.Mirror(Plane.WorldZX);
        secondBezelPt.Transform(mirror);        //Fourth point - Mirrored about ZX plane
        bezelPts.Add(secondBezelPt);

        bezelPts.Add(firstBezelPt);

        Polyline initialBezelFacet = new Polyline(bezelPts);        //Creation of Bezel Facet Polyline
        BezelFacet = ShearCurve(initialBezelFacet, bezelPts[0], bezelPts[2], -CrownAngle);

    }

    //Creation of Pavillion Facet Polyline from Proportion
    public void PavillionFacetCreation(double gemDiameter)
    {
        List<Point3d> pavillionFacetPts = new List<Point3d>();

        Point3d origin = Point3d.Origin;
        Point3d firstPavillionPt = new Point3d(gemDiameter/2, 0, 0);     //First Point - Pavillion Facet polyline start
        pavillionFacetPts.Add(firstPavillionPt);

        double tableRatio = TablePercentage > 1 ? TablePercentage/100.0 : TablePercentage;
        double tableDiameter = tableRatio * gemDiameter;

        double radians = 22.5 * Math.PI / 180;
        double radius = gemDiameter / 2.0;
        Point3d rotatedRightPt = new Point3d(Math.Cos(radians) * radius, Math.Sin(radians) * radius, 0);

        double t = 0.2;     //Pavillion Split of 80:20 ratio

        Point3d secondPavillionPt = new Point3d(origin.X + t * (rotatedRightPt.X - origin.X),
                                                origin.Y + t * (rotatedRightPt.Y - origin.Y),
                                                origin.Z + t * (rotatedRightPt.Z - origin.Z));      //Second Point - Pavillion point on the rotated line
        pavillionFacetPts.Add(secondPavillionPt);

        pavillionFacetPts.Add(origin);      //Third point - Origin
        Transform mirror = Transform.Mirror(Plane.WorldZX);

        secondPavillionPt.Transform(mirror);        //Fourth point - Mirrored about ZX plane
        pavillionFacetPts.Add(secondPavillionPt);

        pavillionFacetPts.Add(firstPavillionPt);        //Fifth point - Closing the loop

        Polyline initialPavillionFacet = new Polyline(pavillionFacetPts);        //Creation of Pavillion Facet Polyline

        PavillionFacet = ShearCurve(initialPavillionFacet, pavillionFacetPts[0], pavillionFacetPts[2], PavillionAngle);

    }

    //Creation of Upper Girdle Facet from previous facets & proportions
    public void UpperGirdleFacetCreation(double gemDiameter)
    {
        Point3d origin = Point3d.Origin;
        List<Point3d> upperGirdleFacetPts = new List<Point3d>();

        double tableRatio = TablePercentage > 1 ? TablePercentage/100.0 : TablePercentage;
        double tableDiameter = tableRatio * gemDiameter;

        double radians = 22.5 * Math.PI / 180;
        double radius = gemDiameter / 2.0;
        Point3d rotatedRightPt = new Point3d(Math.Cos(radians) * radius, Math.Sin(radians) * radius, 0);
        Transform mirror = Transform.Mirror(Plane.WorldZX);

        rotatedRightPt.Transform(mirror);
        upperGirdleFacetPts.Add(rotatedRightPt);        //First point - Mirrored rotated point
        upperGirdleFacetPts.Add(BezelFacet[3]);         //Second point - From Bezel Facet
        upperGirdleFacetPts.Add(BezelFacet[4]);         //Third point - From Bezel Facet
        upperGirdleFacetPts.Add(rotatedRightPt);        //Fourth point - Closing the loop

        UpperGirdleFacet = new Polyline(upperGirdleFacetPts);

    }

    //Creation of Lower Girdle Facet from previous facets & proportions
    public void LowerGirdleFacetCreation(double gemDiameter)
    {
        Point3d origin = Point3d.Origin;
        List<Point3d> lowerGirdleFacetPts = new List<Point3d>();

        double tableRatio = TablePercentage > 1 ? TablePercentage/100.0 : TablePercentage;
        double tableDiameter = tableRatio * gemDiameter;

        double radians = 22.5 * Math.PI / 180;
        double radius = gemDiameter / 2.0;
        Point3d rotatedRightPt = new Point3d(Math.Cos(radians) * radius, Math.Sin(radians) * radius, 0);

        lowerGirdleFacetPts.Add(rotatedRightPt);        //First point - Rotated right point
        lowerGirdleFacetPts.Add(PavillionFacet[1]);     //Second point - From Pavillion Facet
        lowerGirdleFacetPts.Add(PavillionFacet[0]);     //Third point - From Pavillion Facet
        lowerGirdleFacetPts.Add(rotatedRightPt);        //Fourth point - Closing the loop

        LowerGirdleFacet = new Polyline(lowerGirdleFacetPts);
    }


    public static Polyline ShearCurve(Polyline inputCurve, Point3d originPt, Point3d refPt, double angleDeg)
    {
        
        // Convert to radians and compute tangent
        double angleRad = RhinoMath.ToRadians(angleDeg);
        double tanA = System.Math.Tan(angleRad);

        // Apply shear: z' = z + (x - ox) * tan(angle)
        for (int i = 0; i < inputCurve.Count; i++)
        {
            var p = inputCurve[i];
            double dx = p.X - originPt.X;
            double newZ = p.Z + dx * tanA;

            inputCurve[i] = new Point3d(p.X, p.Y, newZ);
        }

        return inputCurve;

    }

}

//Class for Extension of Polyline/Brep edge by a distance.
public class PolyLineExtension
{
    public static Brep Extend(Polyline plFull, int segIndex, double dist, RhinoDoc doc)
    {
        Curve crv = plFull.ToNurbsCurve();

        int n = plFull.Count - 1;
        var pl = new List<Point3d>(n);
        for (int i = 0; i < n; i++) pl.Add(plFull[i]);
        
        // Get the plane of the polyline
        Plane plane;
        double tol = doc.ModelAbsoluteTolerance;
        if (!crv.TryGetPlane(out plane, tol))
        {
            RhinoApp.WriteLine("Polyline is not planar.");
            return null;
        }

        RhinoApp.WriteLine($"Extending (offsetting) segment #{segIndex}.");

        // Extend (parallel offset) the whole selected edge and rebuild the polygon
        var newPl = OffsetOneEdgeParallel(pl, segIndex, dist, plane);
        if (newPl == null || newPl.Count < 3)
        {
            RhinoApp.WriteLine("Failed to compute extended polygon.");
            return null;
        }

        // Create planar Brep from new boundary and add to doc
        var newClosed = new Polyline(newPl);
        newClosed.Add(newClosed[0]); // close
        Brep[] breps = Brep.CreatePlanarBreps(newClosed.ToPolylineCurve(), tol);
        if (breps == null || breps.Length == 0)
        {
            RhinoApp.WriteLine("Failed to create planar face from the extended boundary.");
            return null;
        }

        return breps[0];
    }

    /// <summary>
    /// Offsets (moves parallel) one edge of a closed planar polygon by a signed distance.
    /// Both endpoints are moved coherently; corners are recomputed with neighbor lines.
    /// Input list must contain unique vertices (no trailing duplicate).
    /// </summary>
    static List<Point3d> OffsetOneEdgeParallel(List<Point3d> poly, int segIndex, double dist, Plane plane)
    {
        int n = poly.Count;
        if (n < 3) return null;

        // 3D -> 2D (plane UV)
        var uv = new List<Point2d>(n);
        foreach (var p in poly)
        {
            double u, v;
            plane.ClosestParameter(p, out u, out v);
            uv.Add(new Point2d(u, v));
        }

        int i0 = Mod(segIndex, n);
        int i1 = Mod(segIndex + 1, n);
        int ip = Mod(i0 - 1, n);
        int inx = Mod(i1 + 1, n);

        Point2d Pp = uv[ip];
        Point2d A  = uv[i0];
        Point2d B  = uv[i1];
        Point2d Nn = uv[inx];

        // Segment direction and normals
        Vector2d dir = B - A;
        if (dir.SquareLength < 1e-18) return null;
        dir.Unitize();

        // Polygon orientation
        double area = SignedArea2D(uv);
        Vector2d left = new Vector2d(-dir.Y, dir.X);
        Vector2d outward = (area > 0.0) ? -left : left; // CCW has interior on left

        // Parallel move for entire edge
        Vector2d move = outward * dist;
        Point2d A2 = A + move;
        Point2d B2 = B + move;

        // Intersect the moved edge with neighbor lines to get new corners
        // Prev neighbor infinite line: Pp -> A
        // Next neighbor infinite line: B -> Nn
        Point2d Anew, Bnew;
        bool okA = LineLine2D(Pp, A, A2, B2, out Anew);
        bool okB = LineLine2D(B, Nn, A2, B2, out Bnew);

        if (!okA) Anew = A2; // fallback (parallel neighbors)
        if (!okB) Bnew = B2;

        // Write back
        uv[i0] = Anew;
        uv[i1] = Bnew;

        // Build 3D polygon (unique vertices)
        var outPoly = new List<Point3d>(n);
        for (int i = 0; i < n; i++)
            outPoly.Add(plane.PointAt(uv[i].X, uv[i].Y));

        return outPoly;
    }

    private static int FindNearestSegmentIndex2D(List<Point3d> poly, Plane plane, Point3d pick)
    {
        double pu, pv;
        plane.ClosestParameter(pick, out pu, out pv);
        var P = new Point2d(pu, pv);

        int n = poly.Count;
        int best = -1;
        double bestD2 = double.MaxValue;

        for (int i = 0; i < n; i++)
        {
            Point2d A = ToUV(plane, poly[i]);
            Point2d B = ToUV(plane, poly[(i + 1) % n]);
            double d2 = DistPointToSegment2D(P, A, B);
            if (d2 < bestD2)
            {
                bestD2 = d2;
                best = i;
            }
        }
        return best;
    }

    private static int Mod(int i, int n) => (i % n + n) % n;

    private static Point2d ToUV(Plane plane, Point3d pt)
    {
        double u, v;
        plane.ClosestParameter(pt, out u, out v);
        return new Point2d(u, v);
    }

    private static double SignedArea2D(List<Point2d> pts)
    {
        int n = pts.Count;
        double a = 0.0;
        for (int i = 0; i < n; i++)
        {
            int j = (i + 1) % n;
            a += pts[i].X * pts[j].Y - pts[j].X * pts[i].Y;
        }
        return 0.5 * a;
    }

    private static double DistPointToSegment2D(Point2d P, Point2d A, Point2d B)
    {
        Vector2d AB = B - A;
        double ab2 = AB.X * AB.X + AB.Y * AB.Y;
        if (ab2 < 1e-18) return (P - A).SquareLength;
        double t = ((P.X - A.X) * AB.X + (P.Y - A.Y) * AB.Y) / ab2;
        t = Math.Max(0.0, Math.Min(1.0, t));
        Point2d C = new Point2d(A.X + t * AB.X, A.Y + t * AB.Y);
        return (P - C).SquareLength;
    }

    /// <summary>
    /// Infinite line intersection PQ with RS in 2D.
    /// </summary>
    private static bool LineLine2D(Point2d P, Point2d Q, Point2d R, Point2d S, out Point2d X)
    {
        X = new Point2d();
        Vector2d u = Q - P;
        Vector2d v = S - R;
        double den = u.X * v.Y - u.Y * v.X;
        if (Math.Abs(den) < 1e-14) return false;
        Vector2d w = P - R;
        double t = (v.X * w.Y - v.Y * w.X) / den;
        X = P + t * u;
        return true;
    }
}

//Helper Functions
public class Helper
{
    public static List<Brep> PolarArray(List<Brep> breps, RhinoDoc doc)
    {
        List<Brep> diamondFacets = new List<Brep>();
        // Get center point
        Point3d center = Point3d.Origin;

        // Count
        int count = 8;

        // Angle
        double angleDeg = 360.0;

        double step = RhinoMath.ToRadians(angleDeg / count);

        // Do array
        foreach (Brep brep in breps)
        {
            if (brep == null) continue;

            for (int i = 0; i < count; i++)
            {
                Brep dup = brep.DuplicateBrep();
                var rot = Transform.Rotation(i * step, Vector3d.ZAxis, center);
                dup.Transform(rot);
                diamondFacets.Add(dup);
            }
        }

        return diamondFacets;
    }

    
    public static Brep SplitWithCutters(
        Brep target,
        Brep cutterA,
        Brep cutterB,
        RhinoDoc doc,
        out Brep[] splitPieces
    )
    {
        splitPieces = Array.Empty<Brep>();
        if (target == null) throw new ArgumentNullException(nameof(target));
        if (cutterA == null) throw new ArgumentNullException(nameof(cutterA));
        if (cutterB == null) throw new ArgumentNullException(nameof(cutterB));
        if (doc == null) throw new ArgumentNullException(nameof(doc));

        double tol = doc.ModelAbsoluteTolerance;

        // Split with multiple brep cutters
        // RhinoCommon: Brep.Split(IEnumerable<Brep> cutters, Double intersectionTolerance)
        var pieces = target.Split(new[] { cutterA, cutterB }, tol);

        if (pieces == null || pieces.Length == 0)
        return null;

        // Optional: filter invalids
        var validPieces = pieces.Where(p => p != null && p.IsValid).ToList();
        
        if (validPieces.Count == 0)
        return null;

        // Choose smallest by area
        Brep best = null;
        double bestArea = double.PositiveInfinity;

        foreach (var p in validPieces)
        {
        // Brep.GetArea returns total surface area for the Brep
        double a = p.GetArea();
        RhinoApp.WriteLine($"Valid Pieces Count is {a }");

        if (a < bestArea)
        {
            bestArea = a;
            best = p;
        }
        }

        splitPieces = validPieces.ToArray();
        return best;

    }

}

// DiamondProportions diamond1 = new DiamondProportions(tablePercentage: 62, girdlePercentage: 4.5, pavillionPercentage: 45.5, crownPercentage: 14, crownAngle: 35.9, pavillionAngle: 42.4);
// double gemDiameter = 4.0;

DiamondProportions diamond1 = new DiamondProportions(tablePercentage: 57, girdlePercentage: 4.0, pavillionPercentage: 44.5, crownPercentage: 14.5, crownAngle: 34.0, pavillionAngle: 41.6);
double gemDiameter = 4.5;

// DiamondProportions diamond1 = new DiamondProportions(tablePercentage: 54.0, girdlePercentage: 4.0, pavillionPercentage: 42.0, crownPercentage: 14.0, crownAngle: 30.9, pavillionAngle: 40.0);
// double gemDiameter = 4.0;

diamond1.TableCreation(gemDiameter);
diamond1.BezelFacetCreation(gemDiameter);
diamond1.PavillionFacetCreation(gemDiameter);


Vector3d translation = diamond1.BezelFacet[2] - diamond1.Table[0];
//translation.Unitize();
Transform trasnlationMatrix = Transform.Translation(translation);
diamond1.Table.Transform(trasnlationMatrix);

List<Point3d> starFacetPts = new List<Point3d>{diamond1.Table[1], diamond1.BezelFacet[1], diamond1.Table[0], diamond1.Table[1]};
diamond1.StarFacet = new Polyline(starFacetPts);

diamond1.UpperGirdleFacetCreation(gemDiameter);
diamond1.LowerGirdleFacetCreation(gemDiameter);

doc.Objects.AddPolyline(diamond1.Table);

doc.Objects.AddPolyline(diamond1.BezelFacet);
doc.Objects.AddPolyline(diamond1.PavillionFacet);
// doc.Objects.AddPolyline(diamond1.StarFacet);
// doc.Objects.AddPolyline(diamond1.UpperGirdleFacet);
// doc.Objects.AddPolyline(diamond1.LowerGirdleFacet);

Brep UpperGirdle1 = PolyLineExtension.Extend(diamond1.UpperGirdleFacet, 2, 0.3, doc);
Brep LowerGirdle1 = PolyLineExtension.Extend(diamond1.LowerGirdleFacet, 2, 0.3, doc);

Transform mirror = Transform.Mirror(Plane.WorldZX);

Brep UpperGirdle2 = UpperGirdle1.DuplicateBrep();
UpperGirdle2.Transform(mirror);

Brep LowerGirdle2 = LowerGirdle1.DuplicateBrep();
LowerGirdle2.Transform(mirror);

Brep BezelFacet = Brep.CreatePlanarBreps(diamond1.BezelFacet.ToNurbsCurve()).FirstOrDefault();
Brep StarFacet = Brep.CreatePlanarBreps(diamond1.StarFacet.ToNurbsCurve()).FirstOrDefault();
Brep PavillionFacet = Brep.CreatePlanarBreps(diamond1.PavillionFacet.ToNurbsCurve()).FirstOrDefault();

Brep Table = Brep.CreatePlanarBreps(diamond1.Table.ToNurbsCurve()).FirstOrDefault();
List<Brep> initialDiamondFacets = new List<Brep> {BezelFacet, StarFacet, UpperGirdle1, UpperGirdle2, PavillionFacet, LowerGirdle1, LowerGirdle2};
List<Brep> diamondFacets = Helper.PolarArray(initialDiamondFacets, doc);
diamondFacets.Add(Table);

diamondFacets = Brep.JoinBreps(diamondFacets, doc.ModelAbsoluteTolerance).ToList();

// foreach(Brep brep in diamondFacets) doc.Objects.AddBrep(brep);

Circle girdleCircle = new Circle(gemDiameter/2.0);
Polyline girdleCurve = Polyline.CreateInscribedPolygon(girdleCircle, 64);

double halfHeight = 1;
var dir = new Vector3d(0, 0, 2.0 * halfHeight);

// Surface.CreateExtrusion constructs a surface by extruding along a vector
var srf = Surface.CreateExtrusion(girdleCurve.ToNurbsCurve(), dir);

// Move it down by halfHeight so it spans [-halfHeight, +halfHeight]
srf.Transform(Transform.Translation(0, 0, -halfHeight));
// doc.Objects.AddSurface(srf);

// doc.Objects.AddPolyline(girdleCurve);

List<Brep> finalFacets = new List<Brep>();
foreach(Brep brep in diamondFacets)
{
    // List<Brep> splitBreps = Helper.SplitUsingCurve(brep, srf);
    List<Brep> splitBreps = brep.Split(srf.ToBrep(), doc.ModelAbsoluteTolerance).ToList();
    // foreach(Brep split in splitBreps) doc.Objects.AddBrep(split);
    if(splitBreps.Count > 0)
    {
        Brep best = null;
        double bestArea = double.NegativeInfinity;

        foreach (var b in splitBreps)
        {
            if (b == null) continue;

            // Compute area
            double area = b.GetArea();   // Uses Brep's area computation logic

            if (area > bestArea)
            {
                bestArea = area;
                best = b;
            }
        }

        RhinoApp.WriteLine($"Split length is {splitBreps.Count}");
        finalFacets.Add(best);
        //doc.Objects.AddBrep(best);
    }
    else RhinoApp.WriteLine("Split Failed");
}

Brep crownFacet = AreaMassProperties.Compute(finalFacets[0]).Area > AreaMassProperties.Compute(finalFacets[1]).Area ? finalFacets[1] : finalFacets[0];
Brep pavillionFacet = AreaMassProperties.Compute(finalFacets[0]).Area < AreaMassProperties.Compute(finalFacets[1]).Area ? finalFacets[1] : finalFacets[0];

finalFacets.Clear();

double girdleThickness = diamond1.GirdlePercentage * gemDiameter/100.0;

Vector3d translationUp = new Vector3d(0, 0, girdleThickness);
Transform translationalMatrix = Transform.Translation(translationUp);
crownFacet.Transform(translationalMatrix);

Vector3d translationUpCurve = new Vector3d(0, 0, girdleThickness/2);
Transform translationalMatrixCurve = Transform.Translation(translationUp);
girdleCurve.Transform(translationalMatrixCurve);



finalFacets.Add(crownFacet);
finalFacets.Add(pavillionFacet);


var srf2 = Surface.CreateExtrusion(girdleCurve.ToNurbsCurve(), dir);

// Move it down by halfHeight so it spans [-halfHeight, +halfHeight]
srf2.Transform(Transform.Translation(0, 0, -halfHeight));

// doc.Objects.AddBrep(srf2.ToBrep());

Brep girdle  = Helper.SplitWithCutters(srf2.ToBrep(), crownFacet, pavillionFacet, doc, out Brep[] finalOutput);
RhinoApp.WriteLine($"Output length is {finalOutput.Length}");
// doc.Objects.AddBrep(girdle);
finalFacets.Add(girdle);

Brep Diamond = Brep.JoinBreps(finalFacets, doc.ModelAbsoluteTolerance).ToList().FirstOrDefault();
// doc.Objects.AddBrep(Diamond);

MeshingParameters mp = new MeshingParameters();

// Core quality settings
mp.SimplePlanes = true;
mp.RefineGrid = false;
mp.JaggedSeams = false;

// Density control
mp.MinimumEdgeLength = 0.01;
mp.MaximumEdgeLength = gemDiameter / 5.0;
mp.GridAngle = RhinoMath.ToRadians(5);
mp.GridAspectRatio = 6.0;

// Accuracy
mp.Tolerance = doc.ModelAbsoluteTolerance;
mp.RelativeTolerance = 0.5;

// Generate mesh
Mesh[] meshes = Mesh.CreateFromBrep(Diamond, mp);

if (meshes == null || meshes.Length == 0)
    return;

Mesh finalMesh = new Mesh();
foreach (var m in meshes)
    finalMesh.Append(m);

// Cleanup
finalMesh.Vertices.CombineIdentical(true, true);
finalMesh.Unweld(0, true);  // or skip weld entirely

finalMesh.Normals.ComputeNormals();
finalMesh.Compact();

doc.Objects.AddMesh(finalMesh);
doc.Views.Redraw();