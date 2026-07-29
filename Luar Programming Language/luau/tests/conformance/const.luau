-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
print("testing const declarations")

const answer: number = 42
assert(answer == 42)

const left, right: number = 20, 22
assert(left + right == answer)

const function add(a: number, b: number): number
    return a + b
end
assert(add(left, right) == answer)

do
    const answer = "shadowed"
    assert(answer == "shadowed")

    do
        const answer = true
        assert(answer)
    end

    assert(answer == "shadowed")
end

assert(answer == 42)

return "OK"
