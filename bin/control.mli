type out_message =
  | Suspend
  | Prepare
  | Progress of int
  | Result of int * int

val send : out_channel -> out_message -> unit

val expect_done : in_channel -> unit

type in_message =
  | Restore

val receive : in_channel -> in_message
