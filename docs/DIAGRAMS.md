Empty buffer:

```nomnoml
#.free: dashed
#.write: stroke=green dashed
#.read: fill=#8f8

[LinkedRing|size: 4;read: NULL;write: Cell_0|
[Cells|
[<write>Cell_0]
[Cell_0]o->[<free>Cell_1]
[Cell_1]
[Cell_1]o->[<free>Cell_2]
[Cell_2]
[Cell_2]o->[<free>Cell_3]
[Cell_3]
[Cell_3]o->[Cell_0]
]
]
```

Adding first element to the buffer:

```nomnoml

[Linked Ring|size: 4;read: Cell_0;write: Cell_1|
[Cells|
[<read>Cell_0|owner: 0x1;data: 0xFF]
[Cell_0]o->[<write>Cell_1]
[Cell_1]
[Cell_1]o->[<free>Cell_2]
[Cell_2]
[Cell_2]o->[<free>Cell_3]
[Cell_3]
[Cell_3]o->[Cell_0]
]
]
```

Adding second element to the buffer:

```nomnoml
[LinkedRing|size: 4;read: Cell_0;write: Cell_2|
[Cells|
[<read>Cell_0|owner: 0x1;data: 0xFF]
[Cell_0]o->[Cell_1]
[Cell_1|owner: 0x2;data: 0xCB]
[Cell_1]o->[<write>Cell_2]
[Cell_2]
[Cell_2]o->[<free>Cell_3]
[Cell_3]
[Cell_3]o->[Cell_0]
]
]
```

Full buffer:

```nomnoml
[LinkedRing|size: 4;read: Cell_0;write: NULL|
[Cells|
[<read>Cell_0|owner: 0x1;data: 0xFF]
[Cell_0]o->[Cell_1]
[Cell_1|owner: 0x2;data: 0xCB]
[Cell_1]o->[Cell_2]
[Cell_2|owner: 0x1;data: 0xAB]
[Cell_2]o->[Cell_3]
[Cell_3|owner: 0x2;data: 0x00]
[Cell_3]o->[Cell_0]
]
]
```

Buffer after calling the `lr_get()` function for the owner `0x1`:

```nomnoml
[LinkedRing|size: 4;read: Cell_1;write: Cell_0|
[Cells|
[<read>Cell_1|owner: 0x2;data: 0xCB]
[Cell_1]o->[Cell_2]
[Cell_2|owner: 0x1;data: 0xAB]
[Cell_2]o->[Cell_3]
[Cell_3|owner: 0x2;data: 0x00]
[Cell_3]o->[<write>Cell_0]
[Cell_0]
]
]
```

Buffer after the second call of the `lr_get()` function for the owner `0x1`, which will free `Cell_2` and add them to the end of the buffer:

```nomnoml
[LinkedRing|size: 4;read: Cell_1;write: Cell_0|
[Cells|
[<read>Cell_1|owner: 0x2;data: 0xCB]
[Cell_1]o->[Cell_3]
[Cell_3|owner: 0x2;data: 0x00]
[Cell_3]o->[<write>Cell_0]
[Cell_0]
[<free>Cell_2]
[Cell_0]o->[Cell_2]
]
]
```
