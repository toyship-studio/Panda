# Physics: the SDK part

The script API of the module: `include/Bamboo/Physics3DAPI.hpp`, `CharacterMoverAPI.hpp` and
`Components/Rigidbody2DComponentAPI.hpp`, `Rigidbody3DComponentAPI.hpp`,
`WheelJoint3DComponentAPI.hpp`, implemented in `src/` over the `physics` host table
(`hostApiTable_Find`). Compiled into the PandaSDK library; the headers ship in the SDK package as
`include/Bamboo/…`. Without the module in a game the calls do nothing and return zeros.
