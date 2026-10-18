// Frozen original OpenGL/reference picking implementation; LGPL-2.1.
bool ray_sphere(const LLVector3 &ray_point, const LLVector3 &ray_direction,
                const LLVector3 &sphere_center, F32 sphere_radius,
                LLVector3 &intersection, LLVector3 &intersection_normal)
{
    LLVector3 ray_to_sphere = sphere_center - ray_point;
    F32 dot = ray_to_sphere * ray_direction;

    LLVector3 closest_approach = dot * ray_direction - ray_to_sphere;

    F32 shortest_distance = closest_approach.magVecSquared();
    F32 radius_squared = sphere_radius * sphere_radius;
    if (shortest_distance > radius_squared)
    {
        return false;
    }

    F32 half_chord = (F32) sqrt(radius_squared - shortest_distance);
    closest_approach = sphere_center + closest_approach;            // closest_approach now in absolute coordinates
    intersection = closest_approach + half_chord * ray_direction;
    dot = ray_direction * (intersection - ray_point);
    if (dot < 0.0f)
    {
        // ray shoots away from sphere and is not inside it
        return false;
    }

    shortest_distance = ray_direction * ((closest_approach - half_chord * ray_direction) - ray_point);
    if (shortest_distance > 0.0f)
    {
        // ray enters sphere
        intersection = intersection - (2.0f * half_chord) * ray_direction;
    }
    else
    {
        // do nothing
        // ray starts inside sphere and intersects as it leaves the sphere
    }

    intersection_normal = intersection - sphere_center;
    if (sphere_radius > 0.0f)
    {
        intersection_normal *= 1.0f / sphere_radius;
    }
    else
    {
        intersection_normal.setVec(0.0f, 0.0f, 0.0f);
    }

    return true;
}
bool linesegment_sphere(const LLVector3 &point_a, const LLVector3 &point_b,
                const LLVector3 &sphere_center, F32 sphere_radius,
                LLVector3 &intersection, LLVector3 &intersection_normal)
{
    LLVector3 ray_direction = point_b - point_a;
    F32 segment_length = ray_direction.normVec();

    if (ray_sphere(point_a, ray_direction, sphere_center, sphere_radius, intersection, intersection_normal))
    {
        if (segment_length >= (point_a - intersection).magVec())
        {
            return true;
        }
    }
    return false;
}
