
math.round = function(v)
    return _math_round(v)
end

math.quat_from_euler_angles = function(hori, verti, roll)
    if type(hori) == "table" then
        verti = hori[2]
        roll = hori[3]
        hori = hori[1]
    end
    return _math_quat_fromeuler(hori, verti, roll)
end

math.quat_rotate_vec = function(quat, vector)
    if type(vector) ~= "table" or
            type(quat) ~= "table" then
        error("expected 2 args of type table")
    end
    if #vector < 3 or
            type(vector[1]) ~= "number" or
            type(vector[2]) ~= "number" or
            type(vector[3]) ~= "number" then
        error("vector needs 3 number elements")
    end
    if #quat < 4 or
            type(quat[1]) ~= "number" or
            type(quat[2]) ~= "number" or
            type(quat[3]) ~= "number" or
            type(quat[4]) ~= "number" then
        error("quaternion needs 4 number elements")
    end
    return _math_quat_rotatevec(quat, vector)
end

math.normalize_vec = function(vector)
    if type(vector) ~= "table" or #vector < 3 then
        error("expected a table with 3 numbers")
    end
    return _math_normalize(vector[1], vector[2], vector[3])
end

math.quat_to_euler_angles = function(quat, arg2, arg3, arg4)
    if type(quat) == "number" then
        quat = {quat, arg2, arg3, arg4}
    end
    
end
