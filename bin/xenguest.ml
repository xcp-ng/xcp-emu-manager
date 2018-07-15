let xenguest_path = "/usr/libexec/xen/bin/xenguest"

let control_path domid = Printf.sprintf "/var/xen/xenguest/%d/control" domid

type args = {
  path: string;
  args: string array;
}

let args_of_params {Params.common = common} = {
  path = xenguest_path;
  args = [|
    xenguest_path;
    "-debug";
    "-domid"; string_of_int (common.Params.domid);
    "-controloutfd"; "2";
    "-controlinfd";  "0";
    "-mode"; "listen";
  |];
}

let exec params =
  let args = args_of_params params in
  Unix.execv args.path args.args
