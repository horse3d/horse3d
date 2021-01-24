
# Horse64 Object-Oriented Programming

## Syntax: defining a class (class, new)

To help with object-oriented programming, Horse64 allows
defining custom classes:

```horse64
class MyCircle {
    var radius = 5
    func init {
        print("Circle was created!")
    }
    func print_radius {
        print("Hello! My radius: " + self.radius.as_str)
    }
}

func main {
    var circle_1 = new myclass()
    # ^ Previous line outputs "Circle was created!"

    circle_1.print_radius()
    # ^ Previous line outputs "Hello! My radius: 5"

    print(circle_1.radius)
    # ^ Previous line outputs "5"
}
```

## Semantics: behavior of a class

### What is a class

In Horse64, a class describes, as commonly done in OOP, how all
so-called object instances that were created from it behave.
This is done via so-called attributes that are populated onto
all object instances when they are created.
A class may also be referred to as a class type, since it defines
a new custom user-supplied object type.

### How to create an object instance

An object instance can be created from a class with the `new`
operator. A class can be defined via the `class` statement.

### What are attributes

A class can specify `var` attributes which are initialised with the
given value on each object instance once created. (The default
value assignment with according side effects is evaluated no
earlier than actual object creation.) On each object instance, obtain
attributes via `identifier_referring_to_obj_instance.attribute_name`.
Attributes can be both read and written independently on each
object instance.

A class can specify `func` attributes which can then be called via
`identifier_referring_to_obj_instance.func_attr()`. These attributes
are fixed to the given function and can not be altered. Inside such
a function attribute's statements, the special identifier `self`
refers to the current object instance it runs on.

### Special class function: init

If a class specifies the optional `init` function attribute for its
object instances, then this function will be automatically called
by `new` on creation.

### Special class function: destroy

If a class specifies the `destroy` function attribute, it will be
called when the object is scheduled for garbage collection. **It is
considered a programming error to reintroduce new references to
the object** for when this function terminates, and will cause an
`InvalidDestructorError` raised in the current [execution context](
Horse64%20Concurrency.md#execution-context). The runtime will
attempt to keep the object alive in such a case and prevent follow-up
malfunctions, but do not rely on this behavior.


## Best practices / when NOT to use a class

We recommend you **DO NOT** use a class if all it has is one single
function attribute, and you just set some other attribute values and
then proceed to call that one function. This indicates that you should
just use a plain function with parameters instead. **Overuse of
object-oriented programming obscures code flow with hidden state,**
it becomes no longer obvious what a function call depends on if the
parameters are hidden in an object instance rather than spelled out.

A class / OOP is a good match for anything that would also make
a good self-contained object with complex behavior and state logically,
e.g. a file object with read/write/... and more functions, something
that represents a data record with multiple manipulation methods, etc.


## Deriving classes (extends)

Classes can derive from a different existing class, inheriting
its existing attributes. This is done via `extends`:

```horse64
class Rectangle {
    var width_x = 5
    var width_y = 5
}
class BoxShaped extends Rectangle {
    var height_z = 5
}
func main {
    var box = new BoxShaped()
    print(  # will output: "Size: 5x5x5"
        "Size: %vx%vx%v".format(
            width_x, width_y, width_z
        )
    )
}
```

A class that extends (=derives from) another will then have
the combined attributes on all its object instances. Function
attributes may be specified on the derived class even when
existing on the base class, in which case they will override
them. The special identifier `base` can be used to call the
original function attribute via `base.overridden_func_attr()`.

If `init` is overridden, `base.init(...)` must be called
somewhere inside the new overriding function attribute,
and this call must not be inside any conditional block.

---
This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020-2021 Horse64 Team (See AUTHORS.md)
