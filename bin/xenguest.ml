let xenguest_path = "/usr/libexec/xen/bin/xenguest"

let control_path domid = Printf.sprintf "/var/xen/xenguest/%d/control" domid

type args = {
  path: string;
  args: string array;
}

let args_of_save_params (params : Params.save_params) = {
  path = xenguest_path;
  args = [|
    xenguest_path;
    "-debug";
    "-domid"; string_of_int (params.Params.domid);
    "-controloutfd"; "2";
    "-controlinfd";  "0";
    "-mode"; "listen";
  |];
}

let args_of_restore_params (params : Params.restore_params) = {
  path = xenguest_path;
  args = [|
    xenguest_path;
    "-debug";
    "-domid"; string_of_int (params.Params.domid);
    "-controloutfd"; "2";
    "-controlinfd";  "0";
    "-mode"; "listen";
  |];
}

let args_of_params = function
  | Params.Save params    -> args_of_save_params    params
  | Params.Restore params -> args_of_restore_params params

let exec params =
  let args = args_of_params params in
  Unix.execv args.path args.args
