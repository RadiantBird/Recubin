local shared = {
    value = 7,
}

return {
    answer = 42,
    add = function(left, right)
        return left + right
    end,
    shared = shared,
}
