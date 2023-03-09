locals {
  ami_major_version = split(".","${var.ami_name}")[0]
}

data "aws_ami" "sliderule_cluster_ami" {
  most_recent = true

  filter {
    name   = "name"
    values = [local.ami_major_version]
  }

  filter {
    name   = "virtualization-type"
    values = ["hvm"]
  }

  owners = ["self"]
}